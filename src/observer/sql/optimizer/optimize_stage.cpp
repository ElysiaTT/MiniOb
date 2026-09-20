/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Longda on 2021/4/13.
//

#include <string.h>

#include "optimize_stage.h"

#include "common/conf/ini.h"
#include "common/io/io.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "sql/expr/expression_iterator.h"
#include "sql/operator/logical_operator.h"
#include "sql/operator/table_get_logical_operator.h"
#include "storage/table/table.h"
#include "sql/stmt/stmt.h"
#include "sql/optimizer/cascade/optimizer.h"
#include "sql/optimizer/optimizer_utils.h"

using namespace std;
using namespace common;

static bool expression_contains_subquery(Expression &expression)
{
  if (expression.type() == ExprType::SUBQUERY || expression.type() == ExprType::UNBOUND_SUBQUERY) {
    return true;
  }

  bool contains_subquery = false;
  ExpressionIterator::iterate_child_expr(expression, [&contains_subquery](unique_ptr<Expression> &child) {
    if (!contains_subquery) {
      contains_subquery = expression_contains_subquery(*child);
    }
    return RC::SUCCESS;
  });
  return contains_subquery;
}

static bool expression_contains_null(Expression &expression)
{
  if (expression.type() == ExprType::VALUE && static_cast<ValueExpr &>(expression).get_value().is_null()) {
    return true;
  }

  bool contains_null = false;
  ExpressionIterator::iterate_child_expr(expression, [&contains_null](unique_ptr<Expression> &child) {
    if (!contains_null) {
      contains_null = expression_contains_null(*child);
    }
    return RC::SUCCESS;
  });
  return contains_null;
}

static bool logical_plan_contains_subquery(LogicalOperator &logical_operator)
{
  for (unique_ptr<Expression> &expression : logical_operator.expressions()) {
    if (expression_contains_subquery(*expression)) {
      return true;
    }
  }

  if (logical_operator.type() == LogicalOperatorType::TABLE_GET) {
    auto &table_get = static_cast<TableGetLogicalOperator &>(logical_operator);
    for (unique_ptr<Expression> &predicate : table_get.predicates()) {
      if (expression_contains_subquery(*predicate)) {
        return true;
      }
    }
  }

  for (unique_ptr<LogicalOperator> &child : logical_operator.children()) {
    if (logical_plan_contains_subquery(*child)) {
      return true;
    }
  }
  return false;
}

static bool logical_plan_requires_tuple_execution(LogicalOperator &logical_operator)
{
  if (logical_plan_contains_subquery(logical_operator)) {
    return true;
  }

  for (unique_ptr<Expression> &expression : logical_operator.expressions()) {
    if (expression_contains_null(*expression)) {
      return true;
    }
  }

  if (logical_operator.type() == LogicalOperatorType::TABLE_GET) {
    auto &table_get = static_cast<TableGetLogicalOperator &>(logical_operator);
    const TableMeta &table_meta = table_get.table()->table_meta();
    for (int i = table_meta.sys_field_num(); i < table_meta.field_num(); i++) {
      if (table_meta.field(i)->nullable()) {
        return true;
      }
    }
    for (unique_ptr<Expression> &predicate : table_get.predicates()) {
      if (expression_contains_null(*predicate)) {
        return true;
      }
    }
  }

  for (unique_ptr<LogicalOperator> &child : logical_operator.children()) {
    if (logical_plan_requires_tuple_execution(*child)) {
      return true;
    }
  }
  return false;
}

RC OptimizeStage::handle_request(SQLStageEvent *sql_event)
{
  unique_ptr<LogicalOperator> logical_operator;

  RC rc = create_logical_plan(sql_event, logical_operator);
  if (rc != RC::SUCCESS) {
    if (rc != RC::UNIMPLEMENTED) {
      LOG_WARN("failed to create logical plan. rc=%s", strrc(rc));
    }
    return rc;
  }

  ASSERT(logical_operator, "logical operator is null");

  // TODO: unify the RBO and CBO
  rc = rewrite(logical_operator);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to rewrite plan. rc=%s", strrc(rc));
    return rc;
  }

  // TODO: better way
  logical_operator->generate_general_child();
  Optimizer optimizer;
  // TODO: error handle
  unique_ptr<PhysicalOperator> physical_operator;
  if (sql_event->session_event()->session()->use_cascade()) {
    physical_operator = optimizer.optimize(logical_operator.get());
    if (!physical_operator) {
      rc = RC::INTERNAL;
      LOG_WARN("failed to optimize logical plan. rc=%s", strrc(rc));
      return rc;
    }
    string phys_plan_str = OptimizerUtils::dump_physical_plan(physical_operator);

    LOG_INFO("cascade physical plan:\n%s", phys_plan_str.c_str());
  } else {
    rc = generate_physical_plan(logical_operator, physical_operator, sql_event->session_event()->session());
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to generate physical plan. rc=%s", strrc(rc));
      return rc;
    }
  }

  sql_event->set_operator(std::move(physical_operator));

  return rc;
}

RC OptimizeStage::optimize(unique_ptr<LogicalOperator> &oper)
{
  // do nothing
  return RC::SUCCESS;
}

RC OptimizeStage::generate_physical_plan(
    unique_ptr<LogicalOperator> &logical_operator, unique_ptr<PhysicalOperator> &physical_operator, Session *session)
{
  RC rc = RC::SUCCESS;
  if (session->get_execution_mode() == ExecutionMode::CHUNK_ITERATOR &&
      LogicalOperator::can_generate_vectorized_operator(logical_operator->type()) &&
      !logical_plan_requires_tuple_execution(*logical_operator)) {
    LOG_TRACE("use chunk iterator");
    session->set_used_chunk_mode(true);
    rc    = physical_plan_generator_.create_vec(*logical_operator, physical_operator, session);
  } else {
    LOG_TRACE("use tuple iterator");
    session->set_used_chunk_mode(false);
    rc = physical_plan_generator_.create(*logical_operator, physical_operator, session);
  }
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to create physical operator. rc=%s", strrc(rc));
  }
  return rc;
}

RC OptimizeStage::rewrite(unique_ptr<LogicalOperator> &logical_operator)
{
  RC rc = RC::SUCCESS;

  bool change_made = false;
  do {
    change_made = false;
    rc          = rewriter_.rewrite(logical_operator, change_made);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to do expression rewrite on logical plan. rc=%s", strrc(rc));
      return rc;
    }
  } while (change_made);

  return rc;
}

RC OptimizeStage::create_logical_plan(SQLStageEvent *sql_event, unique_ptr<LogicalOperator> &logical_operator)
{
  Stmt *stmt = sql_event->stmt();
  if (nullptr == stmt) {
    return RC::UNIMPLEMENTED;
  }

  return logical_plan_generator_.create(stmt, logical_operator);
}
