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
// Created by Wangyunlai on 2024/05/29.
//

#pragma once

#include "sql/expr/expression.h"

class Db;

class BinderContext
{
public:
  explicit BinderContext(Db *db = nullptr, BinderContext *parent = nullptr) : db_(db), parent_(parent) {}
  virtual ~BinderContext() = default;

  void add_table(Table *table) { query_tables_.push_back(table); }

  Table *find_table(const char *table_name) const;
  Table *find_table_in_outer_scope(const char *table_name, const BinderContext *&owner) const;
  Table *find_field_in_outer_scope(const char *field_name, const BinderContext *&owner) const;
  void add_correlated_value(
      const shared_ptr<CorrelatedValue> &correlated_value, const BinderContext *owner);

  const vector<Table *> &query_tables() const { return query_tables_; }
  const vector<shared_ptr<CorrelatedValue>> &correlated_values() const { return correlated_values_; }
  Db                    *db() const { return db_; }

private:
  vector<Table *>                     query_tables_;
  vector<shared_ptr<CorrelatedValue>> correlated_values_;
  Db                                 *db_     = nullptr;
  BinderContext                      *parent_ = nullptr;
};

/**
 * @brief 绑定表达式
 * @details 绑定表达式，就是在SQL解析后，得到文本描述的表达式，将表达式解析为具体的数据库对象
 */
class ExpressionBinder
{
public:
  ExpressionBinder(BinderContext &context) : context_(context) {}
  virtual ~ExpressionBinder() = default;

  RC bind_expression(unique_ptr<Expression> &expr, vector<unique_ptr<Expression>> &bound_expressions);

private:
  RC bind_star_expression(unique_ptr<Expression> &star_expr, vector<unique_ptr<Expression>> &bound_expressions);
  RC bind_unbound_field_expression(
      unique_ptr<Expression> &unbound_field_expr, vector<unique_ptr<Expression>> &bound_expressions);
  RC bind_field_expression(unique_ptr<Expression> &field_expr, vector<unique_ptr<Expression>> &bound_expressions);
  RC bind_value_expression(unique_ptr<Expression> &value_expr, vector<unique_ptr<Expression>> &bound_expressions);
  RC bind_cast_expression(unique_ptr<Expression> &cast_expr, vector<unique_ptr<Expression>> &bound_expressions);
  RC bind_comparison_expression(
      unique_ptr<Expression> &comparison_expr, vector<unique_ptr<Expression>> &bound_expressions);
  RC bind_conjunction_expression(
      unique_ptr<Expression> &conjunction_expr, vector<unique_ptr<Expression>> &bound_expressions);
  RC bind_arithmetic_expression(
      unique_ptr<Expression> &arithmetic_expr, vector<unique_ptr<Expression>> &bound_expressions);
  RC bind_aggregate_expression(
      unique_ptr<Expression> &aggregate_expr, vector<unique_ptr<Expression>> &bound_expressions);
  RC bind_subquery_expression(
      unique_ptr<Expression> &subquery_expr, vector<unique_ptr<Expression>> &bound_expressions);

private:
  BinderContext &context_;
};
