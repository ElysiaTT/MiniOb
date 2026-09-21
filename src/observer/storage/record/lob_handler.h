/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "common/lang/mutex.h"
#include "common/lang/sstream.h"
#include "common/types.h"
#include "common/type/attr_type.h"
#include "storage/persist/persist.h"

struct LobLocator
{
  int64_t offset   = 0;
  int32_t length   = 0;
  int32_t reserved = 0;
};

static_assert(sizeof(LobLocator) == TEXT_FIELD_LENGTH);

/**
 * @brief 管理LOB文件中的 LOB 对象
 * @ingroup RecordManager
 */
class LobFileHandler
{
public:
  LobFileHandler() {}

  ~LobFileHandler() { close_file(); }

  RC create_file(const char *file_name);

  RC open_file(const char *file_name);

  RC close_file();

  RC insert_data(int64_t &offset, int64_t length, const char *data);

  RC get_data(int64_t offset, int64_t length, char *data);

private:
  PersistHandler file_;
  mutex          mutex_;
};
