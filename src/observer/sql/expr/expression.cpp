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
// Created by Wangyunlai on 2022/07/05.
//

#include "sql/expr/expression.h"
#include "sql/expr/tuple.h"
#include "sql/expr/arithmetic_operator.hpp"
#include "sql/operator/physical_operator.h"
#include "sql/parser/parse_defs.h"
#include "sql/stmt/select_stmt.h"
#include "session/session.h"
#include "storage/trx/trx.h"

using namespace std;

RC FieldExpr::get_value(const Tuple &tuple, Value &value) const
{
  return tuple.find_cell(TupleCellSpec(table_name(), field_name()), value);
}

bool FieldExpr::equal(const Expression &other) const
{
  if (this == &other) {
    return true;
  }
  if (other.type() != ExprType::FIELD) {
    return false;
  }
  const auto &other_field_expr = static_cast<const FieldExpr &>(other);
  return table_name() == other_field_expr.table_name() && field_name() == other_field_expr.field_name();
}

// TODO: 在进行表达式计算时，`chunk` 包含了所有列，因此可以通过 `field_id` 获取到对应列。
// 后续可以优化成在 `FieldExpr` 中存储 `chunk` 中某列的位置信息。
RC FieldExpr::get_column(Chunk &chunk, Column &column)
{
  if (pos_ != -1) {
    column.reference(chunk.column(pos_));
  } else {
    column.reference(chunk.column(field().meta()->field_id()));
  }
  return RC::SUCCESS;
}

RC CorrelatedFieldExpr::get_value(const Tuple &tuple, Value &value) const
{
  if (!state_->initialized) {
    return RC::INTERNAL;
  }
  value = state_->value;
  return RC::SUCCESS;
}

bool ValueExpr::equal(const Expression &other) const
{
  if (this == &other) {
    return true;
  }
  if (other.type() != ExprType::VALUE) {
    return false;
  }
  const auto &other_value_expr = static_cast<const ValueExpr &>(other);
  return value_.compare(other_value_expr.get_value()) == 0;
}

RC ValueExpr::get_value(const Tuple &tuple, Value &value) const
{
  value = value_;
  return RC::SUCCESS;
}

RC ValueExpr::get_column(Chunk &chunk, Column &column)
{
  column.init(value_, chunk.rows());
  return RC::SUCCESS;
}

/////////////////////////////////////////////////////////////////////////////////

UnboundSubqueryExpr::UnboundSubqueryExpr(ParsedSqlNode *sql_node) : sql_node_(sql_node) {}

UnboundSubqueryExpr::~UnboundSubqueryExpr() = default;

unique_ptr<Expression> UnboundSubqueryExpr::copy() const
{
  auto expression = make_unique<UnboundSubqueryExpr>(nullptr);
  expression->sql_node_ = sql_node_;
  return expression;
}

ParsedSqlNode &UnboundSubqueryExpr::sql_node() const { return *sql_node_; }

struct SubqueryExpr::State
{
  State(unique_ptr<SelectStmt> statement, AttrType value_type, int value_length)
      : statement(std::move(statement)), value_type(value_type), value_length(value_length)
  {
    if (this->statement != nullptr) {
      correlated_values = this->statement->correlated_values();
    }
  }

  unique_ptr<SelectStmt>              statement;
  unique_ptr<PhysicalOperator>        physical_operator;
  Session                            *session = nullptr;
  vector<shared_ptr<CorrelatedValue>> correlated_values;
  vector<Value>                       values;
  AttrType                            value_type     = AttrType::UNDEFINED;
  int                                 value_length   = -1;
  bool                                prepared       = false;
  bool                                materialized   = false;
  bool                                allow_multiple = false;
};

SubqueryExpr::SubqueryExpr(unique_ptr<SelectStmt> statement, AttrType value_type, int value_length)
    : state_(make_shared<State>(std::move(statement), value_type, value_length))
{}

SubqueryExpr::SubqueryExpr(shared_ptr<State> state) : state_(std::move(state)) {}

SubqueryExpr::~SubqueryExpr() = default;

unique_ptr<Expression> SubqueryExpr::copy() const { return unique_ptr<Expression>(new SubqueryExpr(state_)); }

AttrType SubqueryExpr::value_type() const { return state_->value_type; }

int SubqueryExpr::value_length() const { return state_->value_length; }

RC SubqueryExpr::get_value(const Tuple &tuple, Value &value) const
{
  if (!state_->materialized) {
    return RC::INTERNAL;
  }
  if (state_->values.size() != 1) {
    return state_->values.empty() ? RC::EMPTY : RC::INVALID_ARGUMENT;
  }
  value = state_->values.front();
  return RC::SUCCESS;
}

SelectStmt *SubqueryExpr::statement() const { return state_->statement.get(); }

bool SubqueryExpr::correlated() const { return !state_->correlated_values.empty(); }

bool SubqueryExpr::prepared() const { return state_->prepared; }

bool SubqueryExpr::materialized() const { return state_->materialized; }

const vector<Value> &SubqueryExpr::values() const { return state_->values; }

void SubqueryExpr::set_values(vector<Value> values)
{
  state_->values       = std::move(values);
  state_->materialized = true;
}

void SubqueryExpr::set_correlated_plan(
    unique_ptr<PhysicalOperator> physical_operator, Session *session, bool allow_multiple)
{
  state_->physical_operator = std::move(physical_operator);
  state_->session           = session;
  state_->allow_multiple    = allow_multiple;
  state_->prepared          = true;
}

RC SubqueryExpr::evaluate(const Tuple &outer_tuple) const
{
  if (!correlated()) {
    return state_->materialized ? RC::SUCCESS : RC::INTERNAL;
  }
  if (!state_->prepared || state_->physical_operator == nullptr || state_->session == nullptr) {
    return RC::INTERNAL;
  }

  for (const shared_ptr<CorrelatedValue> &correlated_value : state_->correlated_values) {
    Value         value;
    TupleCellSpec spec(correlated_value->field.table_name(), correlated_value->field.field_name());
    RC            rc = outer_tuple.find_cell(spec, value);
    if (OB_SUCC(rc)) {
      correlated_value->value       = std::move(value);
      correlated_value->initialized = true;
    } else if (!correlated_value->initialized) {
      LOG_WARN("failed to resolve correlated field %s.%s from outer tuple",
          correlated_value->field.table_name(), correlated_value->field.field_name());
      return rc;
    }
  }

  Trx *trx = state_->session->current_trx();
  RC   rc  = state_->physical_operator->open(trx);
  if (OB_FAIL(rc)) {
    return rc;
  }

  vector<Value> values;
  while (OB_SUCC(rc = state_->physical_operator->next())) {
    Tuple *tuple = state_->physical_operator->current_tuple();
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

  RC close_rc = state_->physical_operator->close();
  if (OB_SUCC(rc) && OB_FAIL(close_rc)) {
    rc = close_rc;
  }
  if (OB_FAIL(rc)) {
    return rc;
  }

  state_->values       = std::move(values);
  state_->materialized = true;
  if (!state_->allow_multiple && state_->values.size() > 1) {
    LOG_WARN("scalar correlated subquery returned more than one row");
    return RC::INVALID_ARGUMENT;
  }
  return RC::SUCCESS;
}

/////////////////////////////////////////////////////////////////////////////////
CastExpr::CastExpr(unique_ptr<Expression> child, AttrType cast_type) : child_(std::move(child)), cast_type_(cast_type)
{}

CastExpr::~CastExpr() {}

RC CastExpr::cast(const Value &value, Value &cast_value) const
{
  RC rc = RC::SUCCESS;
  if (this->value_type() == value.attr_type()) {
    cast_value = value;
    return rc;
  }
  rc = Value::cast_to(value, cast_type_, cast_value);
  return rc;
}

RC CastExpr::get_value(const Tuple &tuple, Value &result) const
{
  Value value;
  RC rc = child_->get_value(tuple, value);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  return cast(value, result);
}

RC CastExpr::get_column(Chunk &chunk, Column &column)
{
  Column child_column;
  RC rc = child_->get_column(chunk, child_column);
  if (rc != RC::SUCCESS) {
    return rc;
  }
  column.init(cast_type_, child_column.attr_len());
  for (int i = 0; i < child_column.count(); ++i) {
    Value value = child_column.get_value(i);
    Value cast_value;
    rc = cast(value, cast_value);
    if (rc != RC::SUCCESS) {
      return rc;
    }
    column.append_value(cast_value);
  }
  return rc;
}

RC CastExpr::try_get_value(Value &result) const
{
  Value value;
  RC rc = child_->try_get_value(value);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  return cast(value, result);
}

////////////////////////////////////////////////////////////////////////////////

ComparisonExpr::ComparisonExpr(CompOp comp, unique_ptr<Expression> left, unique_ptr<Expression> right)
    : comp_(comp), left_(std::move(left)), right_(std::move(right))
{
}

ComparisonExpr::~ComparisonExpr() {}

static RC compare_scalar_values(const Value &left, const Value &right, int &result)
{
  if (left.attr_type() == right.attr_type() ||
      (is_string_type(left.attr_type()) && is_string_type(right.attr_type())) ||
      (is_numerical_type(left.attr_type()) && is_numerical_type(right.attr_type()))) {
    result = left.compare(right);
    return RC::SUCCESS;
  }

  const int left_to_right_cost =
      DataType::type_instance(left.attr_type())->cast_cost(right.attr_type());
  const int right_to_left_cost =
      DataType::type_instance(right.attr_type())->cast_cost(left.attr_type());
  Value cast_value;
  RC    rc = RC::UNSUPPORTED;
  if (left_to_right_cost <= right_to_left_cost && left_to_right_cost != INT32_MAX) {
    rc = Value::cast_to(left, right.attr_type(), cast_value);
    if (OB_SUCC(rc)) {
      result = cast_value.compare(right);
    }
  } else if (right_to_left_cost != INT32_MAX) {
    rc = Value::cast_to(right, left.attr_type(), cast_value);
    if (OB_SUCC(rc)) {
      result = left.compare(cast_value);
    }
  }
  return rc;
}

RC ComparisonExpr::compare_value(const Value &left, const Value &right, bool &result) const
{
  if (comp_ == IS_OP || comp_ == IS_NOT_OP) {
    result = comp_ == IS_OP ? left.is_null() : !left.is_null();
    return RC::SUCCESS;
  }
  if (left.is_null() || right.is_null()) {
    result = false;
    return RC::SUCCESS;
  }

  int cmp_result = 0;
  RC  rc         = compare_scalar_values(left, right, cmp_result);
  if (OB_FAIL(rc)) {
    return rc;
  }
  result = false;
  switch (comp_) {
    case EQUAL_TO: {
      result = (0 == cmp_result);
    } break;
    case LESS_EQUAL: {
      result = (cmp_result <= 0);
    } break;
    case NOT_EQUAL: {
      result = (cmp_result != 0);
    } break;
    case LESS_THAN: {
      result = (cmp_result < 0);
    } break;
    case GREAT_EQUAL: {
      result = (cmp_result >= 0);
    } break;
    case GREAT_THAN: {
      result = (cmp_result > 0);
    } break;
    case IN_OP:
    case NOT_IN_OP: {
      return RC::INVALID_ARGUMENT;
    }
    case IS_OP:
    case IS_NOT_OP: {
      return RC::INVALID_ARGUMENT;
    }
    default: {
      LOG_WARN("unsupported comparison. %d", comp_);
      rc = RC::INTERNAL;
    } break;
  }

  return rc;
}

RC ComparisonExpr::try_get_value(Value &cell) const
{
  if (left_->type() == ExprType::VALUE && right_->type() == ExprType::VALUE) {
    ValueExpr *  left_value_expr  = static_cast<ValueExpr *>(left_.get());
    ValueExpr *  right_value_expr = static_cast<ValueExpr *>(right_.get());
    const Value &left_cell        = left_value_expr->get_value();
    const Value &right_cell       = right_value_expr->get_value();

    bool value = false;
    RC   rc    = compare_value(left_cell, right_cell, value);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to compare tuple cells. rc=%s", strrc(rc));
    } else {
      cell.set_boolean(value);
    }
    return rc;
  }

  return RC::INVALID_ARGUMENT;
}

RC ComparisonExpr::get_value(const Tuple &tuple, Value &value) const
{
  const SubqueryExpr *left_subquery =
      left_->type() == ExprType::SUBQUERY ? static_cast<const SubqueryExpr *>(left_.get()) : nullptr;
  const SubqueryExpr *right_subquery =
      right_->type() == ExprType::SUBQUERY ? static_cast<const SubqueryExpr *>(right_.get()) : nullptr;

  if (left_subquery != nullptr && left_subquery->correlated()) {
    RC rc = left_subquery->evaluate(tuple);
    if (OB_FAIL(rc)) {
      return rc;
    }
  }
  if (right_subquery != nullptr && right_subquery->correlated()) {
    RC rc = right_subquery->evaluate(tuple);
    if (OB_FAIL(rc)) {
      return rc;
    }
  }

  if (comp_ == IN_OP || comp_ == NOT_IN_OP) {
    if (left_subquery != nullptr || right_subquery == nullptr || !right_subquery->materialized()) {
      return RC::INVALID_ARGUMENT;
    }

    Value left_value;
    RC    rc = left_->get_value(tuple, left_value);
    if (OB_FAIL(rc)) {
      return rc;
    }

    if (left_value.is_null()) {
      value.set_boolean(false);
      return RC::SUCCESS;
    }

    bool found = false;
    bool contains_null = false;
    for (const Value &candidate : right_subquery->values()) {
      if (candidate.is_null()) {
        contains_null = true;
        continue;
      }
      int comparison = 0;
      rc = compare_scalar_values(left_value, candidate, comparison);
      if (OB_FAIL(rc)) {
        return rc;
      }
      if (comparison == 0) {
        found = true;
        break;
      }
    }
    value.set_boolean(comp_ == IN_OP ? found : (!found && !contains_null));
    return RC::SUCCESS;
  }

  if (left_subquery != nullptr || right_subquery != nullptr) {
    if ((left_subquery != nullptr && !left_subquery->materialized()) ||
        (right_subquery != nullptr && !right_subquery->materialized())) {
      return RC::INTERNAL;
    }
    if ((left_subquery != nullptr && right_subquery != nullptr) ||
        (left_subquery != nullptr && left_subquery->values().size() > 1) ||
        (right_subquery != nullptr && right_subquery->values().size() > 1)) {
      return RC::INVALID_ARGUMENT;
    }
    if ((left_subquery != nullptr && left_subquery->values().empty()) ||
        (right_subquery != nullptr && right_subquery->values().empty())) {
      value.set_boolean(false);
      return RC::SUCCESS;
    }
  }

  Value left_value;
  Value right_value;

  RC rc = left_->get_value(tuple, left_value);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get value of left expression. rc=%s", strrc(rc));
    return rc;
  }
  rc = right_->get_value(tuple, right_value);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get value of right expression. rc=%s", strrc(rc));
    return rc;
  }

  bool bool_value = false;

  rc = compare_value(left_value, right_value, bool_value);
  if (rc == RC::SUCCESS) {
    value.set_boolean(bool_value);
  }
  return rc;
}

RC ComparisonExpr::eval(Chunk &chunk, vector<uint8_t> &select)
{
  RC     rc = RC::SUCCESS;
  Column left_column;
  Column right_column;

  rc = left_->get_column(chunk, left_column);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get value of left expression. rc=%s", strrc(rc));
    return rc;
  }
  rc = right_->get_column(chunk, right_column);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get value of right expression. rc=%s", strrc(rc));
    return rc;
  }
  if (left_column.attr_type() != right_column.attr_type()) {
    LOG_WARN("cannot compare columns with different types");
    return RC::INTERNAL;
  }
  if (left_column.attr_type() == AttrType::INTS) {
    rc = compare_column<int>(left_column, right_column, select);
  } else if (left_column.attr_type() == AttrType::FLOATS) {
    rc = compare_column<float>(left_column, right_column, select);
  } else if (left_column.attr_type() == AttrType::CHARS) {
    int rows = 0;
    if (left_column.column_type() == Column::Type::CONSTANT_COLUMN) {
      rows = right_column.count();
    } else {
      rows = left_column.count();
    }
    for (int i = 0; i < rows; ++i) {
      Value left_val = left_column.get_value(i);
      Value right_val = right_column.get_value(i);
      bool        result   = false;
      rc                   = compare_value(left_val, right_val, result);
      if (rc != RC::SUCCESS) {
        LOG_WARN("failed to compare tuple cells. rc=%s", strrc(rc));
        return rc;
      }
      select[i] &= result ? 1 : 0;
    }

  } else {
    LOG_WARN("unsupported data type %d", left_column.attr_type());
    return RC::INTERNAL;
  }
  return rc;
}

template <typename T>
RC ComparisonExpr::compare_column(const Column &left, const Column &right, vector<uint8_t> &result) const
{
  RC rc = RC::SUCCESS;

  bool left_const  = left.column_type() == Column::Type::CONSTANT_COLUMN;
  bool right_const = right.column_type() == Column::Type::CONSTANT_COLUMN;
  if (left_const && right_const) {
    compare_result<T, true, true>((T *)left.data(), (T *)right.data(), left.count(), result, comp_);
  } else if (left_const && !right_const) {
    compare_result<T, true, false>((T *)left.data(), (T *)right.data(), right.count(), result, comp_);
  } else if (!left_const && right_const) {
    compare_result<T, false, true>((T *)left.data(), (T *)right.data(), left.count(), result, comp_);
  } else {
    compare_result<T, false, false>((T *)left.data(), (T *)right.data(), left.count(), result, comp_);
  }
  return rc;
}

////////////////////////////////////////////////////////////////////////////////
ConjunctionExpr::ConjunctionExpr(Type type, vector<unique_ptr<Expression>> &children)
    : conjunction_type_(type), children_(std::move(children))
{}

RC ConjunctionExpr::get_value(const Tuple &tuple, Value &value) const
{
  RC rc = RC::SUCCESS;
  if (children_.empty()) {
    value.set_boolean(true);
    return rc;
  }

  Value tmp_value;
  for (const unique_ptr<Expression> &expr : children_) {
    rc = expr->get_value(tuple, tmp_value);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to get value by child expression. rc=%s", strrc(rc));
      return rc;
    }
    bool bool_value = tmp_value.get_boolean();
    if ((conjunction_type_ == Type::AND && !bool_value) || (conjunction_type_ == Type::OR && bool_value)) {
      value.set_boolean(bool_value);
      return rc;
    }
  }

  bool default_value = (conjunction_type_ == Type::AND);
  value.set_boolean(default_value);
  return rc;
}

////////////////////////////////////////////////////////////////////////////////

ArithmeticExpr::ArithmeticExpr(ArithmeticExpr::Type type, Expression *left, Expression *right)
    : arithmetic_type_(type), left_(left), right_(right)
{}
ArithmeticExpr::ArithmeticExpr(ArithmeticExpr::Type type, unique_ptr<Expression> left, unique_ptr<Expression> right)
    : arithmetic_type_(type), left_(std::move(left)), right_(std::move(right))
{}

bool ArithmeticExpr::equal(const Expression &other) const
{
  if (this == &other) {
    return true;
  }
  if (type() != other.type()) {
    return false;
  }
  auto &other_arith_expr = static_cast<const ArithmeticExpr &>(other);
  if (arithmetic_type_ != other_arith_expr.arithmetic_type() || !left_->equal(*other_arith_expr.left_)) {
    return false;
  }
  if (right_ == nullptr || other_arith_expr.right_ == nullptr) {
    return right_ == nullptr && other_arith_expr.right_ == nullptr;
  }
  return right_->equal(*other_arith_expr.right_);
}
AttrType ArithmeticExpr::value_type() const
{
  if (!right_) {
    return left_->value_type();
  }

  if ((left_->value_type() == AttrType::INTS) &&
   (right_->value_type() == AttrType::INTS) &&
      arithmetic_type_ != Type::DIV) {
    return AttrType::INTS;
  }

  return AttrType::FLOATS;
}

RC ArithmeticExpr::calc_value(const Value &left_value, const Value &right_value, Value &value) const
{
  RC rc = RC::SUCCESS;

  const AttrType target_type = value_type();
  value.set_type(target_type);

  switch (arithmetic_type_) {
    case Type::ADD: {
      rc = Value::add(left_value, right_value, value);
    } break;

    case Type::SUB: {
      rc = Value::subtract(left_value, right_value, value);
    } break;

    case Type::MUL: {
      rc = Value::multiply(left_value, right_value, value);
    } break;

    case Type::DIV: {
      rc = Value::divide(left_value, right_value, value);
    } break;

    case Type::NEGATIVE: {
      rc = Value::negative(left_value, value);
    } break;

    default: {
      rc = RC::INTERNAL;
      LOG_WARN("unsupported arithmetic type. %d", arithmetic_type_);
    } break;
  }
  return rc;
}

template <bool LEFT_CONSTANT, bool RIGHT_CONSTANT>
RC ArithmeticExpr::execute_calc(
    const Column &left, const Column &right, Column &result, Type type, AttrType attr_type) const
{
  RC rc = RC::SUCCESS;
  switch (type) {
    case Type::ADD: {
      if (attr_type == AttrType::INTS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, int, AddOperator>(
            (int *)left.data(), (int *)right.data(), (int *)result.data(), result.capacity());
      } else if (attr_type == AttrType::FLOATS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, float, AddOperator>(
            (float *)left.data(), (float *)right.data(), (float *)result.data(), result.capacity());
      } else {
        rc = RC::UNIMPLEMENTED;
      }
    } break;
    case Type::SUB:
      if (attr_type == AttrType::INTS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, int, SubtractOperator>(
            (int *)left.data(), (int *)right.data(), (int *)result.data(), result.capacity());
      } else if (attr_type == AttrType::FLOATS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, float, SubtractOperator>(
            (float *)left.data(), (float *)right.data(), (float *)result.data(), result.capacity());
      } else {
        rc = RC::UNIMPLEMENTED;
      }
      break;
    case Type::MUL:
      if (attr_type == AttrType::INTS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, int, MultiplyOperator>(
            (int *)left.data(), (int *)right.data(), (int *)result.data(), result.capacity());
      } else if (attr_type == AttrType::FLOATS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, float, MultiplyOperator>(
            (float *)left.data(), (float *)right.data(), (float *)result.data(), result.capacity());
      } else {
        rc = RC::UNIMPLEMENTED;
      }
      break;
    case Type::DIV:
      if (attr_type == AttrType::INTS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, int, DivideOperator>(
            (int *)left.data(), (int *)right.data(), (int *)result.data(), result.capacity());
      } else if (attr_type == AttrType::FLOATS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, float, DivideOperator>(
            (float *)left.data(), (float *)right.data(), (float *)result.data(), result.capacity());
      } else {
        rc = RC::UNIMPLEMENTED;
      }
      break;
    case Type::NEGATIVE:
      if (attr_type == AttrType::INTS) {
        unary_operator<LEFT_CONSTANT, int, NegateOperator>((int *)left.data(), (int *)result.data(), result.capacity());
      } else if (attr_type == AttrType::FLOATS) {
        unary_operator<LEFT_CONSTANT, float, NegateOperator>(
            (float *)left.data(), (float *)result.data(), result.capacity());
      } else {
        rc = RC::UNIMPLEMENTED;
      }
      break;
    default: rc = RC::UNIMPLEMENTED; break;
  }
  if (rc == RC::SUCCESS) {
    result.set_count(result.capacity());
  }
  return rc;
}

RC ArithmeticExpr::get_value(const Tuple &tuple, Value &value) const
{
  RC rc = RC::SUCCESS;

  Value left_value;
  Value right_value;

  rc = left_->get_value(tuple, left_value);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get value of left expression. rc=%s", strrc(rc));
    return rc;
  }
  if (right_) {
    rc = right_->get_value(tuple, right_value);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to get value of right expression. rc=%s", strrc(rc));
      return rc;
    }
  }
  return calc_value(left_value, right_value, value);
}

RC ArithmeticExpr::get_column(Chunk &chunk, Column &column)
{
  RC rc = RC::SUCCESS;
  if (pos_ != -1) {
    column.reference(chunk.column(pos_));
    return rc;
  }
  Column left_column;
  Column right_column;

  rc = left_->get_column(chunk, left_column);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get column of left expression. rc=%s", strrc(rc));
    return rc;
  }
  if (right_) {
    rc = right_->get_column(chunk, right_column);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to get column of right expression. rc=%s", strrc(rc));
      return rc;
    }
  }
  return calc_column(left_column, right_column, column);
}

RC ArithmeticExpr::calc_column(const Column &left_column, const Column &right_column, Column &column) const
{
  RC rc = RC::SUCCESS;

  const AttrType target_type = value_type();
  if (arithmetic_type_ == Type::NEGATIVE) {
    column.init(target_type, left_column.attr_len(), left_column.count());
    const bool left_const = left_column.column_type() == Column::Type::CONSTANT_COLUMN;
    column.set_column_type(left_const ? Column::Type::CONSTANT_COLUMN : Column::Type::NORMAL_COLUMN);
    return left_const
               ? execute_calc<true, true>(left_column, left_column, column, arithmetic_type_, target_type)
               : execute_calc<false, false>(left_column, left_column, column, arithmetic_type_, target_type);
  }

  column.init(target_type, left_column.attr_len(), max(left_column.count(), right_column.count()));
  bool left_const  = left_column.column_type() == Column::Type::CONSTANT_COLUMN;
  bool right_const = right_column.column_type() == Column::Type::CONSTANT_COLUMN;
  if (left_const && right_const) {
    column.set_column_type(Column::Type::CONSTANT_COLUMN);
    rc = execute_calc<true, true>(left_column, right_column, column, arithmetic_type_, target_type);
  } else if (left_const && !right_const) {
    column.set_column_type(Column::Type::NORMAL_COLUMN);
    rc = execute_calc<true, false>(left_column, right_column, column, arithmetic_type_, target_type);
  } else if (!left_const && right_const) {
    column.set_column_type(Column::Type::NORMAL_COLUMN);
    rc = execute_calc<false, true>(left_column, right_column, column, arithmetic_type_, target_type);
  } else {
    column.set_column_type(Column::Type::NORMAL_COLUMN);
    rc = execute_calc<false, false>(left_column, right_column, column, arithmetic_type_, target_type);
  }
  return rc;
}

RC ArithmeticExpr::try_get_value(Value &value) const
{
  RC rc = RC::SUCCESS;

  Value left_value;
  Value right_value;

  rc = left_->try_get_value(left_value);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get value of left expression. rc=%s", strrc(rc));
    return rc;
  }

  if (right_) {
    rc = right_->try_get_value(right_value);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to get value of right expression. rc=%s", strrc(rc));
      return rc;
    }
  }

  return calc_value(left_value, right_value, value);
}

////////////////////////////////////////////////////////////////////////////////

UnboundAggregateExpr::UnboundAggregateExpr(const char *aggregate_name, Expression *child)
    : aggregate_name_(aggregate_name), child_(child)
{}

UnboundAggregateExpr::UnboundAggregateExpr(const char *aggregate_name, unique_ptr<Expression> child)
    : aggregate_name_(aggregate_name), child_(std::move(child))
{}

////////////////////////////////////////////////////////////////////////////////
AggregateExpr::AggregateExpr(Type type, Expression *child) : aggregate_type_(type), child_(child) {}

AggregateExpr::AggregateExpr(Type type, unique_ptr<Expression> child) : aggregate_type_(type), child_(std::move(child))
{}

RC AggregateExpr::get_column(Chunk &chunk, Column &column)
{
  RC rc = RC::SUCCESS;
  if (pos_ != -1) {
    column.reference(chunk.column(pos_));
  } else {
    rc = RC::INTERNAL;
  }
  return rc;
}

bool AggregateExpr::equal(const Expression &other) const
{
  if (this == &other) {
    return true;
  }
  if (other.type() != type()) {
    return false;
  }
  const AggregateExpr &other_aggr_expr = static_cast<const AggregateExpr &>(other);
  return aggregate_type_ == other_aggr_expr.aggregate_type() && child_->equal(*other_aggr_expr.child());
}

unique_ptr<Aggregator> AggregateExpr::create_aggregator() const
{
  unique_ptr<Aggregator> aggregator;
  switch (aggregate_type_) {
    case Type::COUNT: {
      aggregator = make_unique<CountAggregator>();
      break;
    }
    case Type::SUM: {
      aggregator = make_unique<SumAggregator>();
      break;
    }
    case Type::AVG: {
      aggregator = make_unique<AvgAggregator>();
      break;
    }
    case Type::MAX: {
      aggregator = make_unique<MaxAggregator>();
      break;
    }
    case Type::MIN: {
      aggregator = make_unique<MinAggregator>();
      break;
    }
  }
  return aggregator;
}

RC AggregateExpr::get_value(const Tuple &tuple, Value &value) const
{
  return tuple.find_cell(TupleCellSpec(name()), value);
}

RC AggregateExpr::type_from_string(const char *type_str, AggregateExpr::Type &type)
{
  RC rc = RC::SUCCESS;
  if (0 == strcasecmp(type_str, "count")) {
    type = Type::COUNT;
  } else if (0 == strcasecmp(type_str, "sum")) {
    type = Type::SUM;
  } else if (0 == strcasecmp(type_str, "avg")) {
    type = Type::AVG;
  } else if (0 == strcasecmp(type_str, "max")) {
    type = Type::MAX;
  } else if (0 == strcasecmp(type_str, "min")) {
    type = Type::MIN;
  } else {
    rc = RC::INVALID_ARGUMENT;
  }
  return rc;
}
