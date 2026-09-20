/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once

#include "sql/operator/logical_operator.h"

class OrderByLogicalOperator : public LogicalOperator
{
public:
  OrderByLogicalOperator(vector<unique_ptr<Expression>> &&expressions, vector<bool> &&ascending)
      : order_by_expressions_(std::move(expressions)), ascending_(std::move(ascending))
  {}

  LogicalOperatorType type() const override { return LogicalOperatorType::ORDER_BY; }
  OpType              get_op_type() const override { return OpType::LOGICALORDERBY; }

  vector<unique_ptr<Expression>> &order_by_expressions() { return order_by_expressions_; }
  vector<bool>                   &ascending() { return ascending_; }

private:
  vector<unique_ptr<Expression>> order_by_expressions_;
  vector<bool>                   ascending_;
};
