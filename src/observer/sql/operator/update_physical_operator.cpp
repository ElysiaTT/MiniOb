/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/update_physical_operator.h"

#include "common/log/log.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"

RC UpdatePhysicalOperator::open(Trx *trx)
{
  records_.clear();
  if (children_.empty()) {
    return RC::SUCCESS;
  }

  unique_ptr<PhysicalOperator> &child = children_[0];
  RC rc = child->open(trx);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to open child operator for update. rc=%s", strrc(rc));
    return rc;
  }

  while (OB_SUCC(rc = child->next())) {
    Tuple *tuple = child->current_tuple();
    if (tuple == nullptr) {
      child->close();
      return RC::INTERNAL;
    }

    RowTuple     *row_tuple = static_cast<RowTuple *>(tuple);
    const Record &record    = row_tuple->record();
    Record        record_copy;
    rc = record_copy.copy_data(record.data(), record.len());
    if (OB_FAIL(rc)) {
      child->close();
      return rc;
    }
    record_copy.set_rid(record.rid());
    record_copy.set_key(record.key());
    records_.emplace_back(std::move(record_copy));
  }

  RC close_rc = child->close();
  if (rc != RC::RECORD_EOF) {
    return rc;
  }
  if (OB_FAIL(close_rc)) {
    return close_rc;
  }

  vector<Record> new_records;
  new_records.reserve(records_.size());
  for (Record &old_record : records_) {
    Record new_record;
    rc = table_->make_updated_record(old_record, *field_meta_, value_, new_record);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to make updated record. table=%s, field=%s, rc=%s",
          table_->name(), field_meta_->name(), strrc(rc));
      return rc;
    }
    new_records.emplace_back(std::move(new_record));
  }

  size_t updated_count = 0;
  for (; updated_count < records_.size(); updated_count++) {
    rc = trx->update_record(table_, records_[updated_count], new_records[updated_count]);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to update record. table=%s, rid=%s, rc=%s",
          table_->name(), records_[updated_count].rid().to_string().c_str(), strrc(rc));

      while (updated_count > 0) {
        updated_count--;
        RC rollback_rc = trx->update_record(table_, new_records[updated_count], records_[updated_count]);
        if (OB_FAIL(rollback_rc)) {
          LOG_ERROR("failed to rollback updated record. table=%s, rid=%s, rc=%s",
              table_->name(), records_[updated_count].rid().to_string().c_str(), strrc(rollback_rc));
        }
      }
      return rc;
    }
  }

  return RC::SUCCESS;
}
