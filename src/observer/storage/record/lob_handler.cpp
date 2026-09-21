/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "storage/record/lob_handler.h"

RC LobFileHandler::create_file(const char *file_name)
{
  lock_guard<mutex> guard(mutex_);
  return file_.create_file(file_name);
}

RC LobFileHandler::open_file(const char *file_name)
{
  lock_guard<mutex> guard(mutex_);
  std::ifstream file(file_name);
  if (file.good()) {
    return file_.open_file(file_name);
  } else {
    return RC::FILE_NOT_EXIST;
  }
  return RC::INTERNAL;
}

RC LobFileHandler::close_file()
{
  lock_guard<mutex> guard(mutex_);
  return file_.close_file();
}

RC LobFileHandler::insert_data(int64_t &offset, int64_t length, const char *data)
{
  if (length < 0 || length > TEXT_MAX_LENGTH || (length > 0 && data == nullptr)) {
    return RC::INVALID_ARGUMENT;
  }
  lock_guard<mutex> guard(mutex_);
  RC       rc         = RC::SUCCESS;
  int64_t  out_size   = 0;
  int64_t end_offset = 0;
  rc                  = file_.append(length, data, &out_size, &end_offset);
  if (OB_FAIL(rc)) {
    return rc;
  }
  if (out_size != length) {
    return RC::IOERR_WRITE;
  }
  offset = end_offset;

  // Persist the body before publishing its locator in a record or the redo log.
  return file_.sync();
}

RC LobFileHandler::get_data(int64_t offset, int64_t length, char *data)
{
  if (offset < 0 || length < 0 || length > TEXT_MAX_LENGTH || (length > 0 && data == nullptr)) {
    return RC::INVALID_ARGUMENT;
  }
  if (length == 0) {
    return RC::SUCCESS;
  }

  lock_guard<mutex> guard(mutex_);
  int64_t           read_size = 0;
  RC                rc        = file_.read_at(offset, static_cast<int>(length), data, &read_size);
  if (OB_FAIL(rc)) {
    return rc;
  }
  return read_size == length ? RC::SUCCESS : RC::IOERR_READ;
}
