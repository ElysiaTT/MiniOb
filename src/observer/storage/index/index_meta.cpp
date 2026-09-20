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
// Created by Wangyunlai.wyl on 2021/5/18.
//

#include "storage/index/index_meta.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "storage/field/field_meta.h"
#include "storage/table/table_meta.h"
#include "json/json.h"

const static Json::StaticString FIELD_NAME("name");
const static Json::StaticString FIELD_FIELD_NAME("field_name");
const static Json::StaticString FIELD_FIELD_NAMES("field_names");
const static Json::StaticString FIELD_UNIQUE("unique");

RC IndexMeta::init(const char *name, const FieldMeta &field, bool unique)
{
  vector<const FieldMeta *> fields{&field};
  return init(name, fields, unique);
}

RC IndexMeta::init(const char *name, const vector<const FieldMeta *> &fields, bool unique)
{
  if (common::is_blank(name)) {
    LOG_ERROR("Failed to init index, name is empty.");
    return RC::INVALID_ARGUMENT;
  }

  if (fields.empty()) {
    LOG_ERROR("Failed to init index, fields are empty.");
    return RC::INVALID_ARGUMENT;
  }

  name_ = name;
  fields_.clear();
  for (const FieldMeta *field : fields) {
    if (field == nullptr) {
      return RC::INVALID_ARGUMENT;
    }
    fields_.emplace_back(field->name());
  }
  unique_ = unique;
  return RC::SUCCESS;
}

void IndexMeta::to_json(Json::Value &json_value) const
{
  json_value[FIELD_NAME]       = name_;
  json_value[FIELD_FIELD_NAME] = fields_.front();
  Json::Value field_names(Json::arrayValue);
  for (const string &field : fields_) {
    field_names.append(field);
  }
  json_value[FIELD_FIELD_NAMES] = std::move(field_names);
  json_value[FIELD_UNIQUE]     = unique_;
}

RC IndexMeta::from_json(const TableMeta &table, const Json::Value &json_value, IndexMeta &index)
{
  const Json::Value &name_value  = json_value[FIELD_NAME];
  const Json::Value &field_value = json_value[FIELD_FIELD_NAME];
  const Json::Value &fields_value = json_value[FIELD_FIELD_NAMES];
  const Json::Value &unique_value = json_value[FIELD_UNIQUE];
  if (!name_value.isString()) {
    LOG_ERROR("Index name is not a string. json value=%s", name_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  if (!fields_value.isNull() && !fields_value.isArray()) {
    LOG_ERROR("Field names of index [%s] are not an array. json value=%s",
        name_value.asCString(), fields_value.toStyledString().c_str());
    return RC::INTERNAL;
  }
  if (fields_value.isNull() && !field_value.isString()) {
    LOG_ERROR("Field name of index [%s] is not a string. json value=%s",
        name_value.asCString(), field_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  // Older table metadata has no `unique` member. Treat those indexes as
  // ordinary indexes so existing databases remain compatible.
  if (!unique_value.isNull() && !unique_value.isBool()) {
    LOG_ERROR("Unique flag of index [%s] is not a boolean. json value=%s",
        name_value.asCString(), unique_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  vector<const FieldMeta *> fields;
  if (fields_value.isArray()) {
    for (const Json::Value &item : fields_value) {
      if (!item.isString()) {
        return RC::INTERNAL;
      }
      const FieldMeta *field = table.field(item.asCString());
      if (field == nullptr) {
        LOG_ERROR("Deserialize index [%s]: no such field: %s", name_value.asCString(), item.asCString());
        return RC::SCHEMA_FIELD_MISSING;
      }
      fields.emplace_back(field);
    }
  } else {
    const FieldMeta *field = table.field(field_value.asCString());
    if (field == nullptr) {
      LOG_ERROR("Deserialize index [%s]: no such field: %s", name_value.asCString(), field_value.asCString());
      return RC::SCHEMA_FIELD_MISSING;
    }
    fields.emplace_back(field);
  }

  return index.init(name_value.asCString(), fields, unique_value.isBool() && unique_value.asBool());
}

const char *IndexMeta::name() const { return name_.c_str(); }

const char *IndexMeta::field() const { return fields_.empty() ? "" : fields_.front().c_str(); }

void IndexMeta::desc(ostream &os) const
{
  os << "index name=" << name_ << ", fields=";
  for (size_t i = 0; i < fields_.size(); i++) {
    if (i > 0) {
      os << ',';
    }
    os << fields_[i];
  }
  os << ", unique=" << (unique_ ? "true" : "false");
}
