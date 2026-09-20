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
// Created by Wangyunlai on 2022/12/14.
//

#include "common/log/log.h"
#include "sql/expr/expression.h"
#include "session/session.h"
#include "sql/operator/aggregate_vec_physical_operator.h"
#include "sql/operator/calc_logical_operator.h"
#include "sql/operator/calc_physical_operator.h"
#include "sql/operator/delete_logical_operator.h"
#include "sql/operator/delete_physical_operator.h"
#include "sql/operator/explain_logical_operator.h"
#include "sql/operator/explain_physical_operator.h"
#include "sql/operator/expr_vec_physical_operator.h"
#include "sql/operator/group_by_vec_physical_operator.h"
#include "sql/operator/hash_join_physical_operator.h"
#include "sql/operator/index_scan_physical_operator.h"
#include "sql/operator/insert_logical_operator.h"
#include "sql/operator/insert_physical_operator.h"
#include "sql/operator/join_logical_operator.h"
#include "sql/operator/nested_loop_join_physical_operator.h"
#include "sql/operator/predicate_logical_operator.h"
#include "sql/operator/predicate_physical_operator.h"
#include "sql/operator/project_logical_operator.h"
#include "sql/operator/project_physical_operator.h"
#include "sql/operator/project_vec_physical_operator.h"
#include "sql/operator/table_get_logical_operator.h"
#include "sql/operator/table_scan_physical_operator.h"
#include "sql/operator/group_by_logical_operator.h"
#include "sql/operator/group_by_physical_operator.h"
#include "sql/operator/hash_group_by_physical_operator.h"
#include "sql/operator/scalar_group_by_physical_operator.h"
#include "sql/operator/order_by_logical_operator.h"
#include "sql/operator/order_by_physical_operator.h"
#include "sql/operator/table_scan_vec_physical_operator.h"
#include "sql/operator/update_logical_operator.h"
#include "sql/operator/update_physical_operator.h"
#include "sql/expr/expression_iterator.h"
#include "sql/optimizer/logical_plan_generator.h"
#include "sql/optimizer/physical_plan_generator.h"
#include "sql/stmt/select_stmt.h"
#include "storage/index/index.h"
#include "storage/trx/trx.h"

using namespace std;

static RC materialize_subqueries(Expression &expression, Session *session, bool allow_multiple = false)
{
  if (expression.type() == ExprType::SUBQUERY) {
    auto &subquery = static_cast<SubqueryExpr &>(expression);
    if (subquery.correlated() && subquery.prepared()) {
      return RC::SUCCESS;
    }
    if (!subquery.correlated() && subquery.materialized()) {
      return !allow_multiple && subquery.values().size() > 1 ? RC::INVALID_ARGUMENT : RC::SUCCESS;
    }
    if (session == nullptr || subquery.statement() == nullptr) {
      return RC::INVALID_ARGUMENT;
    }

    LogicalPlanGenerator        logical_plan_generator;
    unique_ptr<LogicalOperator> logical_operator;
    RC rc = logical_plan_generator.create(subquery.statement(), logical_operator);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to create subquery logical plan. rc=%s", strrc(rc));
      return rc;
    }

    PhysicalPlanGenerator        physical_plan_generator;
    unique_ptr<PhysicalOperator> physical_operator;
    rc = physical_plan_generator.create(*logical_operator, physical_operator, session);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to create subquery physical plan. rc=%s", strrc(rc));
      return rc;
    }

    if (subquery.correlated()) {
      subquery.set_correlated_plan(std::move(physical_operator), session, allow_multiple);
      return RC::SUCCESS;
    }

    Trx *trx = session->current_trx();
    rc = trx->start_if_need();
    if (OB_FAIL(rc)) {
      return rc;
    }
    rc = physical_operator->open(trx);
    if (OB_FAIL(rc)) {
      return rc;
    }

    vector<Value> values;
    while (OB_SUCC(rc = physical_operator->next())) {
      Tuple *tuple = physical_operator->current_tuple();
      if (tuple == nullptr || tuple->cell_num() != 1) {
        rc = RC::INVALID_ARGUMENT;
        break;
      }

      Value value;
      rc = tuple->cell_at(0, value);
      if (OB_FAIL(rc)) {
        break;
      }
      values.emplace_back(std::move(value));
    }
    if (rc == RC::RECORD_EOF) {
      rc = RC::SUCCESS;
    }

    RC close_rc = physical_operator->close();
    if (OB_SUCC(rc) && OB_FAIL(close_rc)) {
      rc = close_rc;
    }
    if (OB_FAIL(rc)) {
      return rc;
    }

    subquery.set_values(std::move(values));
    if (!allow_multiple && subquery.values().size() > 1) {
      LOG_WARN("scalar subquery returned more than one row");
      return RC::INVALID_ARGUMENT;
    }
    return RC::SUCCESS;
  }

  if (expression.type() == ExprType::COMPARISON) {
    auto &comparison = static_cast<ComparisonExpr &>(expression);
    RC rc = materialize_subqueries(*comparison.left(), session, false);
    if (OB_FAIL(rc)) {
      return rc;
    }
    const bool list_subquery = comparison.comp() == IN_OP || comparison.comp() == NOT_IN_OP;
    return materialize_subqueries(*comparison.right(), session, list_subquery);
  }

  return ExpressionIterator::iterate_child_expr(expression,
      [session](unique_ptr<Expression> &child) { return materialize_subqueries(*child, session, false); });
}

static RC materialize_subqueries(vector<unique_ptr<Expression>> &expressions, Session *session)
{
  for (unique_ptr<Expression> &expression : expressions) {
    RC rc = materialize_subqueries(*expression, session);
    if (OB_FAIL(rc)) {
      return rc;
    }
  }
  return RC::SUCCESS;
}

RC PhysicalPlanGenerator::create(LogicalOperator &logical_operator, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  RC rc = RC::SUCCESS;

  switch (logical_operator.type()) {
    case LogicalOperatorType::CALC: {
      return create_plan(static_cast<CalcLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::TABLE_GET: {
      return create_plan(static_cast<TableGetLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::PREDICATE: {
      return create_plan(static_cast<PredicateLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::PROJECTION: {
      return create_plan(static_cast<ProjectLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::INSERT: {
      return create_plan(static_cast<InsertLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::DELETE: {
      return create_plan(static_cast<DeleteLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::UPDATE: {
      return create_plan(static_cast<UpdateLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::EXPLAIN: {
      return create_plan(static_cast<ExplainLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::JOIN: {
      return create_plan(static_cast<JoinLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::GROUP_BY: {
      return create_plan(static_cast<GroupByLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::ORDER_BY: {
      return create_plan(static_cast<OrderByLogicalOperator &>(logical_operator), oper, session);
    } break;

    default: {
      ASSERT(false, "unknown logical operator type");
      return RC::INVALID_ARGUMENT;
    }
  }
  return rc;
}

RC PhysicalPlanGenerator::create_vec(LogicalOperator &logical_operator, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  RC rc = RC::SUCCESS;

  switch (logical_operator.type()) {
    case LogicalOperatorType::TABLE_GET: {
      return create_vec_plan(static_cast<TableGetLogicalOperator &>(logical_operator), oper, session);
    } break;
    case LogicalOperatorType::PROJECTION: {
      return create_vec_plan(static_cast<ProjectLogicalOperator &>(logical_operator), oper, session);
    } break;
    case LogicalOperatorType::GROUP_BY: {
      return create_vec_plan(static_cast<GroupByLogicalOperator &>(logical_operator), oper, session);
    } break;
    case LogicalOperatorType::EXPLAIN: {
      return create_vec_plan(static_cast<ExplainLogicalOperator &>(logical_operator), oper, session);
    } break;
    default: {
      LOG_WARN("unknown logical operator type: %d", logical_operator.type());
      return RC::INVALID_ARGUMENT;
    }
  }
  return rc;
}

RC PhysicalPlanGenerator::create_plan(TableGetLogicalOperator &table_get_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<Expression>> &predicates = table_get_oper.predicates();
  RC rc = materialize_subqueries(predicates, session);
  if (OB_FAIL(rc)) {
    return rc;
  }
  // 看看是否有可以用于索引查找的表达式
  Table *table = table_get_oper.table();

  Index     *index      = nullptr;
  ValueExpr *value_expr = nullptr;
  vector<char> composite_key;
  size_t       indexed_field_count = 0;
  const TableMeta &table_meta           = table->table_meta();
  for (int index_pos = 0; index_pos < table_meta.index_num(); index_pos++) {
    const IndexMeta *index_meta = table_meta.index(index_pos);
    if (index_meta->fields().size() <= indexed_field_count) {
      continue;
    }

    vector<Value> values;
    bool          all_fields_matched = true;
    for (const string &field_name : index_meta->fields()) {
      const Value *matched_value = nullptr;
      for (const unique_ptr<Expression> &expr : predicates) {
        if (expr->type() != ExprType::COMPARISON) {
          continue;
        }
        auto comparison_expr = static_cast<ComparisonExpr *>(expr.get());
        if (comparison_expr->comp() != EQUAL_TO) {
          continue;
        }

        Expression *left_expr  = comparison_expr->left().get();
        Expression *right_expr = comparison_expr->right().get();
        FieldExpr  *field_expr  = nullptr;
        ValueExpr  *constant    = nullptr;
        if (left_expr->type() == ExprType::FIELD && right_expr->type() == ExprType::VALUE) {
          field_expr = static_cast<FieldExpr *>(left_expr);
          constant   = static_cast<ValueExpr *>(right_expr);
        } else if (right_expr->type() == ExprType::FIELD && left_expr->type() == ExprType::VALUE) {
          field_expr = static_cast<FieldExpr *>(right_expr);
          constant   = static_cast<ValueExpr *>(left_expr);
        }

        if (field_expr != nullptr && field_expr->field().table() == table &&
            0 == strcmp(field_expr->field_name(), field_name.c_str())) {
          matched_value = &constant->get_value();
          break;
        }
      }

      if (matched_value == nullptr) {
        all_fields_matched = false;
        break;
      }
      values.emplace_back(*matched_value);
    }

    if (!all_fields_matched) {
      continue;
    }
    Index *candidate = table->find_index(index_meta->name());
    vector<char> candidate_key;
    if (candidate != nullptr && OB_SUCC(candidate->make_key(values, candidate_key))) {
      index = candidate;
      composite_key.swap(candidate_key);
      indexed_field_count = index_meta->fields().size();
    }
  }
  for (auto &expr : predicates) {
    if (index != nullptr) {
      break;
    }
    if (expr->type() == ExprType::COMPARISON) {
      auto comparison_expr = static_cast<ComparisonExpr *>(expr.get());
      // 简单处理，就找等值查询
      if (comparison_expr->comp() != EQUAL_TO) {
        continue;
      }

      unique_ptr<Expression> &left_expr  = comparison_expr->left();
      unique_ptr<Expression> &right_expr = comparison_expr->right();
      // 左右比较的一边最少是一个值
      if (left_expr->type() != ExprType::VALUE && right_expr->type() != ExprType::VALUE) {
        continue;
      }

      FieldExpr *field_expr = nullptr;
      if (left_expr->type() == ExprType::FIELD) {
        ASSERT(right_expr->type() == ExprType::VALUE, "right expr should be a value expr while left is field expr");
        field_expr = static_cast<FieldExpr *>(left_expr.get());
        value_expr = static_cast<ValueExpr *>(right_expr.get());
      } else if (right_expr->type() == ExprType::FIELD) {
        ASSERT(left_expr->type() == ExprType::VALUE, "left expr should be a value expr while right is a field expr");
        field_expr = static_cast<FieldExpr *>(right_expr.get());
        value_expr = static_cast<ValueExpr *>(left_expr.get());
      }

      if (field_expr == nullptr) {
        continue;
      }

      const Field &field = field_expr->field();
      index              = table->find_index_by_field(field.field_name());
      if (nullptr != index) {
        break;
      }
    }
  }

  if (index != nullptr) {
    if (composite_key.empty()) {
      ASSERT(value_expr != nullptr, "got an index but value expr is null ?");
      vector<Value> values{value_expr->get_value()};
      rc = index->make_key(values, composite_key);
      if (OB_FAIL(rc)) {
        return rc;
      }
    }
    IndexScanPhysicalOperator *index_scan_oper = new IndexScanPhysicalOperator(table,
        index,
        table_get_oper.read_write_mode(),
        composite_key,
        true /*left_inclusive*/,
        composite_key,
        true /*right_inclusive*/);

    index_scan_oper->set_predicates(std::move(predicates));
    oper = unique_ptr<PhysicalOperator>(index_scan_oper);
    LOG_TRACE("use index scan");
  } else {
    auto table_scan_oper = new TableScanPhysicalOperator(table, table_get_oper.read_write_mode());
    table_scan_oper->set_predicates(std::move(predicates));
    oper = unique_ptr<PhysicalOperator>(table_scan_oper);
    LOG_TRACE("use table scan");
  }

  return RC::SUCCESS;
}

RC PhysicalPlanGenerator::create_plan(PredicateLogicalOperator &pred_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<LogicalOperator>> &children_opers = pred_oper.children();
  ASSERT(children_opers.size() == 1, "predicate logical operator's sub oper number should be 1");

  LogicalOperator &child_oper = *children_opers.front();

  unique_ptr<PhysicalOperator> child_phy_oper;
  RC                           rc = create(child_oper, child_phy_oper, session);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to create child operator of predicate operator. rc=%s", strrc(rc));
    return rc;
  }

  vector<unique_ptr<Expression>> &expressions = pred_oper.expressions();
  ASSERT(expressions.size() == 1, "predicate logical operator's children should be 1");

  rc = materialize_subqueries(expressions, session);
  if (OB_FAIL(rc)) {
    return rc;
  }

  unique_ptr<Expression> expression = std::move(expressions.front());
  oper = unique_ptr<PhysicalOperator>(new PredicatePhysicalOperator(std::move(expression)));
  oper->add_child(std::move(child_phy_oper));
  return rc;
}

RC PhysicalPlanGenerator::create_plan(ProjectLogicalOperator &project_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<LogicalOperator>> &child_opers = project_oper.children();

  unique_ptr<PhysicalOperator> child_phy_oper;

  RC rc = materialize_subqueries(project_oper.expressions(), session);
  if (OB_FAIL(rc)) {
    return rc;
  }
  if (!child_opers.empty()) {
    LogicalOperator *child_oper = child_opers.front().get();

    rc = create(*child_oper, child_phy_oper, session);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to create project logical operator's child physical operator. rc=%s", strrc(rc));
      return rc;
    }
  }

  auto project_operator = make_unique<ProjectPhysicalOperator>(std::move(project_oper.expressions()));
  if (child_phy_oper) {
    project_operator->add_child(std::move(child_phy_oper));
  }

  oper = std::move(project_operator);

  LOG_TRACE("create a project physical operator");
  return rc;
}

RC PhysicalPlanGenerator::create_plan(InsertLogicalOperator &insert_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  Table                  *table           = insert_oper.table();
  vector<vector<Value>>  &values          = insert_oper.values();
  InsertPhysicalOperator *insert_phy_oper = new InsertPhysicalOperator(table, std::move(values));
  oper.reset(insert_phy_oper);
  return RC::SUCCESS;
}

RC PhysicalPlanGenerator::create_plan(DeleteLogicalOperator &delete_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<LogicalOperator>> &child_opers = delete_oper.children();

  unique_ptr<PhysicalOperator> child_physical_oper;

  RC rc = RC::SUCCESS;
  if (!child_opers.empty()) {
    LogicalOperator *child_oper = child_opers.front().get();

    rc = create(*child_oper, child_physical_oper, session);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to create physical operator. rc=%s", strrc(rc));
      return rc;
    }
  }

  oper = unique_ptr<PhysicalOperator>(new DeletePhysicalOperator(delete_oper.table()));

  if (child_physical_oper) {
    oper->add_child(std::move(child_physical_oper));
  }
  return rc;
}

RC PhysicalPlanGenerator::create_plan(UpdateLogicalOperator &update_oper, unique_ptr<PhysicalOperator> &oper, Session *session)
{
  vector<unique_ptr<LogicalOperator>> &child_opers = update_oper.children();
  unique_ptr<PhysicalOperator> child_physical_oper;

  RC rc = RC::SUCCESS;
  if (!child_opers.empty()) {
    rc = create(*child_opers.front(), child_physical_oper, session);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to create child physical operator for update. rc=%s", strrc(rc));
      return rc;
    }
  }

  oper = make_unique<UpdatePhysicalOperator>(update_oper.table(), update_oper.field_meta(), update_oper.value());
  if (child_physical_oper) {
    oper->add_child(std::move(child_physical_oper));
  }
  return RC::SUCCESS;
}

RC PhysicalPlanGenerator::create_plan(ExplainLogicalOperator &explain_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<LogicalOperator>> &child_opers = explain_oper.children();

  RC rc = RC::SUCCESS;

  unique_ptr<PhysicalOperator> explain_physical_oper(new ExplainPhysicalOperator);
  for (unique_ptr<LogicalOperator> &child_oper : child_opers) {
    unique_ptr<PhysicalOperator> child_physical_oper;
    rc = create(*child_oper, child_physical_oper, session);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to create child physical operator. rc=%s", strrc(rc));
      return rc;
    }

    explain_physical_oper->add_child(std::move(child_physical_oper));
  }

  oper = std::move(explain_physical_oper);
  return rc;
}

RC PhysicalPlanGenerator::create_plan(JoinLogicalOperator &join_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  RC rc = RC::SUCCESS;

  vector<unique_ptr<LogicalOperator>> &child_opers = join_oper.children();
  if (child_opers.size() != 2) {
    LOG_WARN("join operator should have 2 children, but have %d", child_opers.size());
    return RC::INTERNAL;
  }
  if (session->hash_join_on() && can_use_hash_join(join_oper)) {
    // your code here
  } else {
    unique_ptr<PhysicalOperator> join_physical_oper(new NestedLoopJoinPhysicalOperator());
    for (auto &child_oper : child_opers) {
      unique_ptr<PhysicalOperator> child_physical_oper;
      rc = create(*child_oper, child_physical_oper, session);
      if (rc != RC::SUCCESS) {
        LOG_WARN("failed to create physical child oper. rc=%s", strrc(rc));
        return rc;
      }

      join_physical_oper->add_child(std::move(child_physical_oper));
    }

    oper = std::move(join_physical_oper);
  }
  return rc;
}

bool PhysicalPlanGenerator::can_use_hash_join(JoinLogicalOperator &join_oper)
{
  // your code here
  return false;
}

RC PhysicalPlanGenerator::create_plan(CalcLogicalOperator &logical_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  RC rc = materialize_subqueries(logical_oper.expressions(), session);
  if (OB_FAIL(rc)) {
    return rc;
  }

  CalcPhysicalOperator *calc_oper = new CalcPhysicalOperator(std::move(logical_oper.expressions()));
  oper.reset(calc_oper);
  return rc;
}

RC PhysicalPlanGenerator::create_plan(GroupByLogicalOperator &logical_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  RC rc = RC::SUCCESS;

  vector<unique_ptr<Expression>> &group_by_expressions = logical_oper.group_by_expressions();
  unique_ptr<GroupByPhysicalOperator> group_by_oper;
  if (group_by_expressions.empty()) {
    group_by_oper = make_unique<ScalarGroupByPhysicalOperator>(std::move(logical_oper.aggregate_expressions()));
  } else {
    group_by_oper = make_unique<HashGroupByPhysicalOperator>(std::move(logical_oper.group_by_expressions()),
        std::move(logical_oper.aggregate_expressions()));
  }

  ASSERT(logical_oper.children().size() == 1, "group by operator should have 1 child");

  LogicalOperator             &child_oper = *logical_oper.children().front();
  unique_ptr<PhysicalOperator> child_physical_oper;
  rc = create(child_oper, child_physical_oper, session);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to create child physical operator of group by operator. rc=%s", strrc(rc));
    return rc;
  }

  group_by_oper->add_child(std::move(child_physical_oper));

  oper = std::move(group_by_oper);
  return rc;
}

RC PhysicalPlanGenerator::create_plan(OrderByLogicalOperator &logical_oper,
    unique_ptr<PhysicalOperator> &oper, Session *session)
{
  ASSERT(logical_oper.children().size() == 1, "order by operator should have 1 child");

  RC rc = materialize_subqueries(logical_oper.order_by_expressions(), session);
  if (OB_FAIL(rc)) {
    return rc;
  }

  unique_ptr<PhysicalOperator> child_physical_oper;
  rc = create(*logical_oper.children().front(), child_physical_oper, session);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to create child physical operator of order by. rc=%s", strrc(rc));
    return rc;
  }

  auto order_by_oper = make_unique<OrderByPhysicalOperator>(
      std::move(logical_oper.order_by_expressions()), std::move(logical_oper.ascending()));
  order_by_oper->add_child(std::move(child_physical_oper));
  oper = std::move(order_by_oper);
  return RC::SUCCESS;
}

RC PhysicalPlanGenerator::create_vec_plan(TableGetLogicalOperator &table_get_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<Expression>> &predicates = table_get_oper.predicates();
  RC rc = materialize_subqueries(predicates, session);
  if (OB_FAIL(rc)) {
    return rc;
  }
  Table *table = table_get_oper.table();
  TableScanVecPhysicalOperator *table_scan_oper = new TableScanVecPhysicalOperator(table, table_get_oper.read_write_mode());
  table_scan_oper->set_predicates(std::move(predicates));
  oper = unique_ptr<PhysicalOperator>(table_scan_oper);
  LOG_TRACE("use vectorized table scan");

  return RC::SUCCESS;
}

RC PhysicalPlanGenerator::create_vec_plan(GroupByLogicalOperator &logical_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  RC rc = RC::SUCCESS;
  unique_ptr<PhysicalOperator> physical_oper = nullptr;
  if (logical_oper.group_by_expressions().empty()) {
    physical_oper = make_unique<AggregateVecPhysicalOperator>(std::move(logical_oper.aggregate_expressions()));
  } else {
    physical_oper = make_unique<GroupByVecPhysicalOperator>(
      std::move(logical_oper.group_by_expressions()), std::move(logical_oper.aggregate_expressions()));

  }

  ASSERT(logical_oper.children().size() == 1, "group by operator should have 1 child");

  LogicalOperator             &child_oper = *logical_oper.children().front();
  unique_ptr<PhysicalOperator> child_physical_oper;
  rc = create_vec(child_oper, child_physical_oper, session);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to create child physical operator of group by(vec) operator. rc=%s", strrc(rc));
    return rc;
  }

  physical_oper->add_child(std::move(child_physical_oper));

  oper = std::move(physical_oper);
  return rc;

  return RC::SUCCESS;
}

RC PhysicalPlanGenerator::create_vec_plan(ProjectLogicalOperator &project_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<LogicalOperator>> &child_opers = project_oper.children();

  unique_ptr<PhysicalOperator> child_phy_oper;

  RC rc = materialize_subqueries(project_oper.expressions(), session);
  if (OB_FAIL(rc)) {
    return rc;
  }
  if (!child_opers.empty()) {
    LogicalOperator *child_oper = child_opers.front().get();
    rc                          = create_vec(*child_oper, child_phy_oper, session);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to create project logical operator's child physical operator. rc=%s", strrc(rc));
      return rc;
    }
  }

  auto project_operator = make_unique<ProjectVecPhysicalOperator>(std::move(project_oper.expressions()));

  if (child_phy_oper != nullptr) {
    vector<Expression *> expressions;
    for (auto &expr : project_operator->expressions()) {
      expressions.push_back(expr.get());
    }
    auto expr_operator = make_unique<ExprVecPhysicalOperator>(std::move(expressions));
    expr_operator->add_child(std::move(child_phy_oper));
    project_operator->add_child(std::move(expr_operator));
  }

  oper = std::move(project_operator);

  LOG_TRACE("create a project physical operator");
  return rc;
}


RC PhysicalPlanGenerator::create_vec_plan(ExplainLogicalOperator &explain_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<LogicalOperator>> &child_opers = explain_oper.children();

  RC rc = RC::SUCCESS;
  // reuse `ExplainPhysicalOperator` in explain vectorized physical plan
  unique_ptr<PhysicalOperator> explain_physical_oper(new ExplainPhysicalOperator);
  for (unique_ptr<LogicalOperator> &child_oper : child_opers) {
    unique_ptr<PhysicalOperator> child_physical_oper;
    rc = create_vec(*child_oper, child_physical_oper, session);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to create child physical operator. rc=%s", strrc(rc));
      return rc;
    }

    explain_physical_oper->add_child(std::move(child_physical_oper));
  }

  oper = std::move(explain_physical_oper);
  return rc;
}
