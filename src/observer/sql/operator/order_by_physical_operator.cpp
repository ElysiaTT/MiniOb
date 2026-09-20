/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "sql/operator/order_by_physical_operator.h"

#include <algorithm>

#include "common/log/log.h"

using namespace std;

RC OrderByPhysicalOperator::open(Trx *trx)
{
  ASSERT(children_.size() == 1, "order by operator should have exactly one child");
  ASSERT(order_by_expressions_.size() == ascending_.size(), "order expressions and directions must match");

  rows_.clear();
  next_index_     = 0;
  current_index_  = 0;
  current_valid_  = false;

  PhysicalOperator &child = *children_.front();
  RC rc = child.open(trx);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to open order by child. rc=%s", strrc(rc));
    return rc;
  }

  while (OB_SUCC(rc = child.next())) {
    Tuple *tuple = child.current_tuple();
    if (tuple == nullptr) {
      child.close();
      return RC::INTERNAL;
    }

    SortRow row;
    row.keys.reserve(order_by_expressions_.size());
    for (const unique_ptr<Expression> &expression : order_by_expressions_) {
      Value key;
      rc = expression->get_value(*tuple, key);
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to evaluate order by expression. rc=%s", strrc(rc));
        child.close();
        rows_.clear();
        return rc;
      }
      row.keys.emplace_back(std::move(key));
    }

    rc = ValueListTuple::make(*tuple, row.tuple);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to materialize tuple for order by. rc=%s", strrc(rc));
      child.close();
      rows_.clear();
      return rc;
    }
    rows_.emplace_back(std::move(row));
  }

  if (rc != RC::RECORD_EOF) {
    LOG_WARN("failed to consume order by child. rc=%s", strrc(rc));
    child.close();
    rows_.clear();
    return rc;
  }

  stable_sort(rows_.begin(), rows_.end(), [this](const SortRow &left, const SortRow &right) {
    for (size_t i = 0; i < left.keys.size(); i++) {
      const int result = left.keys[i].compare(right.keys[i]);
      if (result != 0) {
        return ascending_[i] ? result < 0 : result > 0;
      }
    }
    return false;
  });

  return RC::SUCCESS;
}

RC OrderByPhysicalOperator::next()
{
  if (next_index_ >= rows_.size()) {
    current_valid_ = false;
    return RC::RECORD_EOF;
  }

  current_index_ = next_index_++;
  current_valid_ = true;
  return RC::SUCCESS;
}

RC OrderByPhysicalOperator::close()
{
  RC rc = RC::SUCCESS;
  if (!children_.empty()) {
    rc = children_.front()->close();
  }
  rows_.clear();
  next_index_ = 0;
  current_index_ = 0;
  current_valid_ = false;
  return rc;
}

Tuple *OrderByPhysicalOperator::current_tuple()
{
  return current_valid_ ? &rows_[current_index_].tuple : nullptr;
}

RC OrderByPhysicalOperator::tuple_schema(TupleSchema &schema) const
{
  return children_.front()->tuple_schema(schema);
}
