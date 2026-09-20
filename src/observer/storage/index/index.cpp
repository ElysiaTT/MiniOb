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
// Created by wangyunlai.wyl on 2021/5/19.
//

#include "storage/index/index.h"
#include <algorithm>
#include <cstring>

RC Index::init(const IndexMeta &index_meta, const vector<const FieldMeta *> &field_metas)
{
  if (field_metas.empty()) {
    return RC::INVALID_ARGUMENT;
  }
  index_meta_ = index_meta;
  field_metas_.clear();
  for (const FieldMeta *field_meta : field_metas) {
    if (field_meta == nullptr) {
      return RC::INVALID_ARGUMENT;
    }
    field_metas_.emplace_back(*field_meta);
  }
  return RC::SUCCESS;
}

RC Index::make_key(const vector<Value> &values, vector<char> &key) const
{
  if (values.size() != field_metas_.size()) {
    return RC::INVALID_ARGUMENT;
  }

  size_t key_length = 0;
  for (const FieldMeta &field : field_metas_) {
    key_length += field.len();
  }
  key.assign(key_length, 0);

  size_t offset = 0;
  for (size_t i = 0; i < values.size(); i++) {
    const FieldMeta &field = field_metas_[i];
    Value            value;
    RC               rc = RC::SUCCESS;
    if (values[i].attr_type() == field.type()) {
      value = values[i];
    } else {
      rc = Value::cast_to(values[i], field.type(), value);
      if (OB_FAIL(rc)) {
        return rc;
      }
    }

    const size_t copy_length = field.type() == AttrType::CHARS
                                   ? std::min(static_cast<size_t>(field.len()), static_cast<size_t>(value.length()))
                                   : static_cast<size_t>(field.len());
    memcpy(key.data() + offset, value.data(), copy_length);
    offset += field.len();
  }
  return RC::SUCCESS;
}
