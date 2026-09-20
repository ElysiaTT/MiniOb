/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "common/type/date_type.h"

#include <cstdio>
#include <algorithm>
#include <cctype>

#include "common/lang/comparator.h"
#include "common/value.h"
#include "storage/common/column.h"

RC DateType::parse_date(const string &text, int &encoded_date)
{
  const size_t first_dash = text.find('-');
  const size_t second_dash = first_dash == string::npos ? string::npos : text.find('-', first_dash + 1);
  if (first_dash != 4 || second_dash == string::npos || text.find('-', second_dash + 1) != string::npos ||
      second_dash - first_dash < 2 || second_dash - first_dash > 3 ||
      text.size() - second_dash < 2 || text.size() - second_dash > 3) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  const auto all_digits = [](const string &part) {
    return !part.empty() && std::all_of(part.begin(), part.end(), [](unsigned char ch) { return std::isdigit(ch); });
  };
  const string year_text = text.substr(0, first_dash);
  const string month_text = text.substr(first_dash + 1, second_dash - first_dash - 1);
  const string day_text = text.substr(second_dash + 1);
  if (!all_digits(year_text) || !all_digits(month_text) || !all_digits(day_text)) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  const int year = stoi(year_text);
  const int month = stoi(month_text);
  const int day = stoi(day_text);
  if (year < 1 || month < 1 || month > 12) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  static constexpr int DAYS_PER_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int max_day = DAYS_PER_MONTH[month - 1];
  const bool leap_year = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  if (month == 2 && leap_year) {
    max_day = 29;
  }
  if (day < 1 || day > max_day) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  encoded_date = year * 10000 + month * 100 + day;
  return RC::SUCCESS;
}

int DateType::compare(const Value &left, const Value &right) const
{
  ASSERT(left.attr_type() == AttrType::DATES && right.attr_type() == AttrType::DATES, "invalid date type");
  int left_date = left.get_int();
  int right_date = right.get_int();
  return common::compare_int(&left_date, &right_date);
}

int DateType::compare(const Column &left, const Column &right, int left_idx, int right_idx) const
{
  ASSERT(left.attr_type() == AttrType::DATES && right.attr_type() == AttrType::DATES, "invalid date column type");
  return common::compare_int(
      (void *)&((int *)left.data())[left_idx], (void *)&((int *)right.data())[right_idx]);
}

RC DateType::to_string(const Value &val, string &result) const
{
  const int encoded_date = val.get_int();
  const int year = encoded_date / 10000;
  const int month = encoded_date / 100 % 100;
  const int day = encoded_date % 100;
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
  result = buffer;
  return RC::SUCCESS;
}

RC DateType::set_value_from_str(Value &val, const string &data) const
{
  int encoded_date = 0;
  RC rc = parse_date(data, encoded_date);
  if (OB_SUCC(rc)) {
    val.set_date(encoded_date);
  }
  return rc;
}
