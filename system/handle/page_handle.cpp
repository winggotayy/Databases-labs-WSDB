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
// Created by ziqi on 2024/7/27.
//

#include "page_handle.h"
#include "../../../common/error.h"
#include "storage/buffer/buffer_pool_manager.h"

namespace wsdb {
PageHandle::PageHandle(const TableHeader *tab_hdr, Page *page, char *bit_map, char *slots_mem)
   : tab_hdr_(tab_hdr), page_(page), bitmap_(bit_map), slots_mem_(slots_mem)
{
 WSDB_ASSERT(BITMAP_SIZE(tab_hdr->rec_per_page_) == tab_hdr->bitmap_size_, "bitmap size not match");
}
void PageHandle::WriteSlot(size_t slot_id, const char *null_map, const char *data, bool update)
{
 WSDB_THROW(WSDB_EXCEPTION_EMPTY, "");
}

void PageHandle::ReadSlot(size_t slot_id, char *null_map, char *data) { WSDB_THROW(WSDB_EXCEPTION_EMPTY, ""); }
auto PageHandle::ReadChunk(const RecordSchema *chunk_schema) -> ChunkUptr { WSDB_THROW(WSDB_EXCEPTION_EMPTY, ""); }

NAryPageHandle::NAryPageHandle(const TableHeader *tab_hdr, Page *page)
   : PageHandle(
         tab_hdr, page, page->GetData() + PAGE_HEADER_SIZE, page->GetData() + PAGE_HEADER_SIZE + tab_hdr->bitmap_size_)
{}

void NAryPageHandle::WriteSlot(size_t slot_id, const char *null_map, const char *data, bool update)
{
 WSDB_ASSERT(slot_id < tab_hdr_->rec_per_page_, "slot_id out of range");
 WSDB_ASSERT(BitMap::GetBit(bitmap_, slot_id) == update, fmt::format("update: {}", update));
 // a record consists of null map and data
 size_t rec_full_size = tab_hdr_->nullmap_size_ + tab_hdr_->rec_size_;
 memcpy(slots_mem_ + slot_id * rec_full_size, null_map, tab_hdr_->nullmap_size_);
 memcpy(slots_mem_ + slot_id * rec_full_size + tab_hdr_->nullmap_size_, data, tab_hdr_->rec_size_);
}

void NAryPageHandle::ReadSlot(size_t slot_id, char *null_map, char *data)
{
 WSDB_ASSERT(slot_id < tab_hdr_->rec_per_page_, "slot_id out of range");
 WSDB_ASSERT(BitMap::GetBit(bitmap_, slot_id) == true, "slot is empty");
 size_t rec_full_size = tab_hdr_->nullmap_size_ + tab_hdr_->rec_size_;
 memcpy(null_map, slots_mem_ + slot_id * rec_full_size, tab_hdr_->nullmap_size_);
 memcpy(data, slots_mem_ + slot_id * rec_full_size + tab_hdr_->nullmap_size_, tab_hdr_->rec_size_);
}

PAXPageHandle::PAXPageHandle(
   const TableHeader *tab_hdr, Page *page, const RecordSchema *schema, const std::vector<size_t> &offsets)
   : PageHandle(tab_hdr, page, page->GetData() + PAGE_HEADER_SIZE,
         page->GetData() + PAGE_HEADER_SIZE + tab_hdr->bitmap_size_),
     schema_(schema),
     offsets_(offsets)
{}

PAXPageHandle::~PAXPageHandle() = default;

// slot memery
// | nullmap_1, nullmap_2, ... , nullmap_n|
// | field_1_1, field_1_2, ... , field_1_n |
// | field_2_1, field_2_2, ... , field_2_n |
// ...
// | field_m_1, field_m_2, ... , field_m_n |
void PAXPageHandle::WriteSlot(size_t slot_id, const char *null_map, const char *data, bool update)
{
  // WSDB_STUDENT_TODO(l1, f2);
  WSDB_ASSERT(slot_id < tab_hdr_->rec_per_page_, "slot_id out of range");
  WSDB_ASSERT(BitMap::GetBit(bitmap_, slot_id) == update, fmt::format("update: {}", update));

  // Step 1: 处理 null_map 的写入
  memcpy(slots_mem_ + slot_id * tab_hdr_->nullmap_size_, null_map, tab_hdr_->nullmap_size_);

  // Step 2: 写入每个字段的数据
  std::vector<char*> field_ptrs;
  for (size_t i = 0; i < schema_->GetFieldCount(); ++i)
  {
    const auto& field = schema_->GetFieldAt(i);
    size_t field_size = field.field_.field_size_;
    // 计算字段内存位置
    field_ptrs.push_back(slots_mem_ + offsets_[i] + slot_id * field_size);
  }
  // 使用 vector 中存储的地址进行数据写入
  size_t total_field_offset = 0;
  for (size_t i = 0; i < field_ptrs.size(); ++i)
  {
    memcpy(field_ptrs[i], data + total_field_offset, schema_->GetFieldAt(i).field_.field_size_);
    total_field_offset += schema_->GetFieldAt(i).field_.field_size_;
  }
}

void PAXPageHandle::ReadSlot(size_t slot_id, char *null_map, char *data)
{
  // WSDB_STUDENT_TODO(l1, f2);
  WSDB_ASSERT(slot_id < tab_hdr_->rec_per_page_, "slot_id out of range");
  WSDB_ASSERT(BitMap::GetBit(bitmap_, slot_id) == true, "slot is empty");

  // Step 1: 处理 null_map 的读取
  memcpy(null_map, slots_mem_ + slot_id * tab_hdr_->nullmap_size_, tab_hdr_->nullmap_size_);

  // Step 2: 读取每个字段的数据
  std::vector<char*> field_ptrs;
  for (size_t i = 0; i < schema_->GetFieldCount(); ++i)
  {
    const auto& field = schema_->GetFieldAt(i);
    size_t field_size = field.field_.field_size_;
    // 计算字段内存位置
    field_ptrs.push_back(slots_mem_ + offsets_[i] + slot_id * field_size);
  }

  // 使用 vector 中存储的地址进行数据读取
  size_t total_field_offset = 0;
  for (size_t i = 0; i < field_ptrs.size(); ++i)
  {
    memcpy(data + total_field_offset, field_ptrs[i], schema_->GetFieldAt(i).field_.field_size_);
    total_field_offset += schema_->GetFieldAt(i).field_.field_size_;
  }
}
/* ^__^ note FOR MYSELF: write and read */ /* if there's bugs afterwards CHECK */

auto PAXPageHandle::ReadChunk(const RecordSchema *chunk_schema) -> ChunkUptr
{
 std::vector<ArrayValueSptr> col_arrs;
 col_arrs.reserve(chunk_schema->GetFieldCount());

 // read data each field and construct ArrayValue
 // WSDB_STUDENT_TODO(l1, f2);;

 // 获取每页的记录数
 size_t total_records = tab_hdr_->rec_per_page_;

 // 定义一个 lambda 函数 processField，用于处理每个字段的读取和 ArrayValue 对象的构建
 auto processField = [&](size_t field_idx) -> ArrayValueSptr
 {
   const auto& field = chunk_schema->GetFieldAt(field_idx); // 获取字段的元数据
   size_t field_size = field.field_.field_size_; // 字段数据的大小
   size_t field_offset = offsets_[field_idx]; // 字段数据在记录中的偏移量
   // 创建一个新的 ArrayValue 对象，用于存储该字段的所有值
   auto array_value = std::make_shared<ArrayValue>();

   // 遍历每个记录槽位
   for (size_t slot_id = 0; slot_id < total_records; ++slot_id)
   {
     if (!BitMap::GetBit(bitmap_, slot_id)) continue;

     // 获取字段的 null_map 位的指针
     char* null_map_ptr = slots_mem_ + slot_id * tab_hdr_->nullmap_size_;
     if (BitMap::GetBit(null_map_ptr, field_idx))
     {
       // 追加一个 null 值到 array_value
       array_value->Append(ValueFactory::CreateNullValue(field.field_.field_type_));
     }
     else
     {
       // 计算字段数据的位置，并读取数据
       char* field_data = slots_mem_ + field_offset + slot_id * field_size;
       // 创建字段值，并追加到 array_value
       auto value = ValueFactory::CreateValue(field.field_.field_type_, field_data, field_size);
       array_value->Append(value);
     }
   }
   return array_value;
 };

 // 对于 chunk_schema 中的每个字段，调用 processField 并存储结果
 for (size_t field_idx = 0; field_idx < chunk_schema->GetFieldCount(); ++field_idx)
 {
   col_arrs.push_back(processField(field_idx));
 }

 return std::make_unique<Chunk>(chunk_schema, std::move(col_arrs));
}
}  // namespace wsdb