/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once

#include "sql/operator/physical_operator.h"

class OrderByPhysicalOperator : public PhysicalOperator
{
public:
  OrderByPhysicalOperator(vector<unique_ptr<Expression>> &&expressions, vector<bool> &&ascending)
      : order_by_expressions_(std::move(expressions)), ascending_(std::move(ascending))
  {}

  PhysicalOperatorType type() const override { return PhysicalOperatorType::ORDER_BY; }
  OpType               get_op_type() const override { return OpType::ORDERBY; }

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override;
  RC     tuple_schema(TupleSchema &schema) const override;

private:
  struct SortRow
  {
    vector<Value>  keys;
    ValueListTuple tuple;
  };

  vector<unique_ptr<Expression>> order_by_expressions_;
  vector<bool>                   ascending_;
  vector<SortRow>                rows_;
  size_t                         next_index_ = 0;
  size_t                         current_index_ = 0;
  bool                           current_valid_ = false;
};
