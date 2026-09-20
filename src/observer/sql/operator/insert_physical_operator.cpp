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
// Created by WangYunlai on 2021/6/9.
//

#include "sql/operator/insert_physical_operator.h"
#include "sql/stmt/insert_stmt.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"

using namespace std;

InsertPhysicalOperator::InsertPhysicalOperator(Table *table, vector<vector<Value>> &&values)
    : table_(table), values_(std::move(values))
{}

RC InsertPhysicalOperator::open(Trx *trx)
{
  vector<Record> records;
  records.reserve(values_.size());

  // Construct every record first so a conversion error cannot leave a partial insert.
  for (const vector<Value> &values : values_) {
    Record record;
    RC rc = table_->make_record(static_cast<int>(values.size()), values.data(), record);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to make record. rc=%s", strrc(rc));
      return rc;
    }
    records.emplace_back(std::move(record));
  }

  size_t inserted_count = 0;
  for (; inserted_count < records.size(); inserted_count++) {
    RC rc = trx->insert_record(table_, records[inserted_count]);
    if (rc == RC::SUCCESS) {
      continue;
    }

    LOG_WARN("failed to insert record by transaction. row=%zu, rc=%s", inserted_count, strrc(rc));
    while (inserted_count > 0) {
      inserted_count--;
      RC rollback_rc = trx->delete_record(table_, records[inserted_count]);
      if (rollback_rc != RC::SUCCESS) {
        LOG_ERROR("failed to rollback inserted record. row=%zu, rc=%s", inserted_count, strrc(rollback_rc));
      }
    }
    return rc;
  }
  return RC::SUCCESS;
}

RC InsertPhysicalOperator::next() { return RC::RECORD_EOF; }

RC InsertPhysicalOperator::close() { return RC::SUCCESS; }
