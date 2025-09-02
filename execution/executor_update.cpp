
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

#include "executor_update.h"

namespace wsdb {

UpdateExecutor::UpdateExecutor(AbstractExecutorUptr child, TableHandle *tbl, std::list<IndexHandle *> indexes,
    std::vector<std::pair<RTField, ValueSptr>> updates)
    : AbstractExecutor(DML),
      child_(std::move(child)),
      tbl_(tbl),
      indexes_(std::move(indexes)),
      updates_(std::move(updates)),
      is_end_(false)
{
  std::vector<RTField> fields(1);
  fields[0]   = RTField{.field_ = {.field_name_ = "updated", .field_size_ = sizeof(int), .field_type_ = TYPE_INT}};
  out_schema_ = std::make_unique<RecordSchema>(fields);
}

void UpdateExecutor::Init() { WSDB_FETAL("UpdateExecutor does not support Init"); }

void UpdateExecutor::Next()
{
  // number of updated records
  int count = 0;

  //WSDB_STUDENT_TODO(l2, t1);
  child_->Init();

  for (; !child_->IsEnd(); child_->Next())
  {
    auto old_record = child_->GetRecord();
    if (!old_record)
    {
      continue;
    }
    const auto *schema = old_record->GetSchema(); // 模式（字段信息）
    // 哈希表快速映射（需要更新的字段&新值）
    std::unordered_map<std::string, ValueSptr> updates_map;
    for (const auto &[field, value] : updates_)
    {
      updates_map[field.field_.field_name_] = value;
    }

    // 构造更新后的记录值
    std::vector<ValueSptr> new_values;
    new_values.reserve(schema->GetFieldCount());  // 为新值向量预分配内存

    for (size_t i = 0; i < schema->GetFieldCount(); i++)
    {
      const auto &field_name = schema->GetFieldAt(i).field_.field_name_;
      if (updates_map.find(field_name) != updates_map.end())
      {
        new_values.push_back(updates_map[field_name]);  // 插入新值
      }
      else
      {
        new_values.push_back(old_record->GetValueAt(i));  // 插入原始值
      }
    }

    // 构造新的记录
    Record new_record(schema, new_values, old_record->GetRID());
    // 更新表中的记录
    tbl_->UpdateRecord(old_record->GetRID(), new_record);

    // 更新所有相关索引
    for (auto *index : indexes_)
    {
      if (index)
      {
        index->UpdateRecord(*old_record, new_record);
      }
    }

    count++;
  }

  std::vector<ValueSptr> values{ValueFactory::CreateIntValue(count)};
  record_ = std::make_unique<Record>(out_schema_.get(), values, INVALID_RID);
  is_end_ = true;
}

auto UpdateExecutor::IsEnd() const -> bool { return is_end_; }

}  // namespace wsdb