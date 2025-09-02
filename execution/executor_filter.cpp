/*------------------------------------------------------------------------------
 - Copyright (c) 2024. Websoft research group, Nanjing University.
 -
 - This program is free software: you can redistribute it and/or modify
 - it under the terms of the GNU General Public License as published by
 - the Free Software Foundation, either version 3 of the License, or
 - (at your option) any later version.
 -
 - This program is distributed in the hope that it will be useful,
 - but WITHOUT ANY WARRANTY; without even the implied warranty of
 - MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 - GNU General Public License for more details.
 -
 - You should have received a copy of the GNU General Public License
 - along with this program.  If not, see <https://www.gnu.org/licenses/>.
 -----------------------------------------------------------------------------*/

//
// Created by ziqi on 2024/7/18.
//

#include "executor_filter.h"

namespace wsdb {

FilterExecutor::FilterExecutor(AbstractExecutorUptr child, std::function<bool(const Record &)> filter)
    : AbstractExecutor(Basic), child_(std::move(child)), filter_(std::move(filter))
{}
void FilterExecutor::Init()
{
  //WSDB_STUDENT_TODO(l2, t1);
  child_->Init();
  record_ = nullptr;

  for (; !child_->IsEnd(); child_->Next())
  {
    auto current_record = child_->GetRecord();
    if (current_record && filter_(*current_record))
    {
      record_ = child_->GetRecord();
      break;
    }
  }
}

void FilterExecutor::Next()
{
  //WSDB_STUDENT_TODO(l2, t1);
  for (child_->Next(); !child_->IsEnd(); child_->Next())
  {
    auto current_record = child_->GetRecord();
    if (current_record && filter_(*current_record))
    {
      // 如果找到符合过滤条件的记录，保存并退出函数
      record_ = child_->GetRecord();
      return;
    }
  }

  // 如果没有符合条件的记录，设置记录为空
  record_ = nullptr;
}

auto FilterExecutor::IsEnd() const -> bool
{
  //WSDB_STUDENT_TODO(l2, t1);
  return record_ == nullptr;
}

auto FilterExecutor::GetOutSchema() const -> const RecordSchema * { return child_->GetOutSchema(); }
}  // namespace wsdb
