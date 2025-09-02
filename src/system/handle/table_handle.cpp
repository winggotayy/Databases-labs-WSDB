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
// Created by ziqi on 2024/7/19.
//

#include "table_handle.h"
namespace wsdb {

TableHandle::TableHandle(DiskManager* disk_manager, BufferPoolManager* buffer_pool_manager, table_id_t table_id,
   TableHeader& hdr, RecordSchemaUptr& schema, StorageModel storage_model)
   : tab_hdr_(hdr),
     table_id_(table_id),
     disk_manager_(disk_manager),
     buffer_pool_manager_(buffer_pool_manager),
     schema_(std::move(schema)),
     storage_model_(storage_model)
{
 // set table id for table handle;
 schema_->SetTableId(table_id_);
 if (storage_model_ == PAX_MODEL) {
   field_offset_.resize(schema_->GetFieldCount());
   // calculate offsets of fields
   // WSDB_STUDENT_TODO(l1, f2);
   size_t offSet = tab_hdr_.nullmap_size_ * tab_hdr_.rec_per_page_;  // 初始化为nullmap区域的大小

   for (size_t i = 0; i < schema_->GetFieldCount(); i++)
   {
     field_offset_[i] = offSet; // 记录字段的偏移量
     size_t fieldSize = schema_->GetFieldAt(i).field_.field_size_;
     offSet += fieldSize * tab_hdr_.rec_per_page_; // 更新偏移量，跳过当前字段的所有记录
   }
 }
}

auto TableHandle::GetRecord(const RID& rid) -> RecordUptr
{
  auto nullmap = std::make_unique<char[]>(tab_hdr_.nullmap_size_);
  auto data = std::make_unique<char[]>(tab_hdr_.rec_size_);
  // WSDB_STUDENT_TODO(l1, t3);
  auto page_handle = FetchPageHandle(rid.PageID());

  auto bitMap = page_handle->GetBitmap();
  // No record in the slot
  if (!BitMap::GetBit(bitMap, rid.SlotID()))
  {
    buffer_pool_manager_->UnpinPage(table_id_, rid.PageID(), false);
    WSDB_THROW(WSDB_PAGE_MISS, fmt::format("Page: {}", rid.PageID()));
  }
  else
  {
    page_handle->ReadSlot(rid.SlotID(), nullmap.get(), data.get());
    buffer_pool_manager_->UnpinPage(table_id_, rid.PageID(), false);
    return std::make_unique<Record>(schema_.get(), nullmap.get(), data.get(), rid);
  }
}

auto TableHandle::GetChunk(page_id_t pid, const RecordSchema* chunk_schema) -> ChunkUptr
{
  // WSDB_STUDENT_TODO(l1, f2);
  // 获取页面句柄
  auto page_handle = FetchPageHandle(pid);
  // 使用页面句柄读取数据块（Chunk）
  auto chunk = page_handle->ReadChunk(chunk_schema); // 根据传入的chunk_schema来解析数据
  // 读取完成后，将页面从缓冲池中解除固定（不再需要此页面）
  buffer_pool_manager_->UnpinPage(table_id_, pid, false);
  return chunk; // 返回读取的数据块
}

auto TableHandle::InsertRecord(const Record& record) -> RID
{
  // WSDB_STUDENT_TODO(l1, t3);

  // 1. create a page handle using CreatePageHandle
  auto new_page_handle = CreateNewPageHandle();

  // 2. get an empty slot in the page
  auto bitMap = new_page_handle->GetBitmap();
  auto empty_slot = BitMap::FindFirst(bitMap, tab_hdr_.rec_per_page_, 0, false);

  // 3. write the record into the slot
  new_page_handle->WriteSlot(empty_slot, record.GetNullMap(), record.GetData(), false);

  // 4. update the bitmap and the number of records in the page header
  BitMap::SetBit(bitMap, empty_slot, true); // slot已占用
  tab_hdr_.rec_num_++; // 全局记录计数递增：增加表级的总记录数
  auto recordNum = new_page_handle->GetPage()->GetRecordNum(); // 获取该页当前记录的数量
  new_page_handle->GetPage()->SetRecordNum(recordNum++); // 将页内记录数自增并更新到当前页

  // 5. if the page is full after inserting the record
  if(recordNum == tab_hdr_.rec_per_page_)
  {
    // update the first free page id in the file header
    tab_hdr_.first_free_page_ = new_page_handle->GetPage()->GetNextFreePageId(); // 指向下一个空闲页
    // set the next page id of the current page
    new_page_handle->GetPage()->SetNextFreePageId(INVALID_PAGE_ID); // 标记此页不再是空闲页，表示不可插入
  }

  // 6. unpin the page
  buffer_pool_manager_->UnpinPage(table_id_, new_page_handle->GetPage()->GetPageId(), true);

  // @param record
  // @return rid of the inserted record
  return RID(new_page_handle->GetPage()->GetPageId(), empty_slot);
}

void TableHandle::InsertRecord(const RID& rid, const Record& record)
{
 if (rid.PageID() == INVALID_PAGE_ID) {
   WSDB_THROW(WSDB_PAGE_MISS, fmt::format("Page: {}", rid.PageID()));
 }
 // WSDB_STUDENT_TODO(l1, t3);

 // 2. fetch the page handle and check the bitmap
 auto new_page_handle = FetchPageHandle(rid.PageID());
 auto bitMap = new_page_handle->GetBitmap();
 // if the slot is not empty, throw WSDB_RECORD_EXISTS
 if (BitMap::GetBit(bitMap, rid.SlotID()))
 {
   buffer_pool_manager_->UnpinPage(table_id_, rid.PageID(), false);
   WSDB_THROW(WSDB_RECORD_EXISTS, fmt::format("Record: {}", rid.SlotID()));
 }

 // 3. do the rest of the steps in InsertRecord 3-6
 // 3. write the record into the slot
 new_page_handle->WriteSlot(rid.SlotID(), record.GetNullMap(), record.GetData(), false);

 // 4. update the bitmap and the number of records in the page header
 BitMap::SetBit(bitMap, rid.SlotID(), true); // slot已占用
 tab_hdr_.rec_num_++; // 全局记录计数递增：增加表级的总记录数
 auto recordNum = new_page_handle->GetPage()->GetRecordNum(); // 获取该页当前记录的数量
 new_page_handle->GetPage()->SetRecordNum(recordNum++); // 将页内记录数自增并更新到当前页

 // 5. if the page is full after inserting the record
 if(recordNum == tab_hdr_.rec_per_page_)
 {
   // update the first free page id in the file header
   tab_hdr_.first_free_page_ = new_page_handle->GetPage()->GetNextFreePageId(); // 指向下一个空闲页
   // set the next page id of the current page
   new_page_handle->GetPage()->SetNextFreePageId(INVALID_PAGE_ID); // 标记此页不再是空闲页，表示不可插入
 }

 // 6. unpin the page
 buffer_pool_manager_->UnpinPage(table_id_, new_page_handle->GetPage()->GetPageId(), true);
}

void TableHandle::DeleteRecord(const RID& rid)
{
  // WSDB_STUDENT_TODO(l1, t3);

  auto page_handle = FetchPageHandle(rid.PageID());
  auto bitMap = page_handle->GetBitmap();

  // 1. if the slot is empty
  if (!BitMap::GetBit(bitMap, rid.SlotID()))
  {
    // unpin the page
    buffer_pool_manager_->UnpinPage(table_id_, rid.PageID(), false);
    // throw WSDB_RECORD_MISS
    WSDB_THROW(WSDB_RECORD_MISS, fmt::format("Record: {}", rid.SlotID()));
  }

  // 2. update the bitmap and the number of records in the page header
  BitMap::SetBit(bitMap, rid.SlotID(), false); // slot未占用 // 和上面的倒反
  tab_hdr_.rec_num_--;
  auto recordNum = page_handle->GetPage()->GetRecordNum();
  page_handle->GetPage()->SetRecordNum(recordNum--);

  // 3. if the page is not full after deleting the record
  if(recordNum < tab_hdr_.rec_per_page_)
  {
    // update the first free page id in the file header
    page_handle->GetPage()->SetNextFreePageId(tab_hdr_.first_free_page_);
    // and the next page id in the page header
    tab_hdr_.first_free_page_ = rid.PageID();
  }

  // 4. unpin the page
  buffer_pool_manager_->UnpinPage(table_id_, rid.PageID(), true);
}

void TableHandle::UpdateRecord(const RID& rid, const Record& record)
{
  // WSDB_STUDENT_TODO(l1, t3);

  auto page_handle = FetchPageHandle(rid.PageID());
  auto bitMap = page_handle->GetBitmap();

  // 1. if the slot is empty
  if (!BitMap::GetBit(bitMap, rid.SlotID()))
  {
    // unpin the page
    buffer_pool_manager_->UnpinPage(table_id_, rid.PageID(), false);
    // throw WSDB_RECORD_MISS
    WSDB_THROW(WSDB_RECORD_MISS, fmt::format("Record: {}", rid.SlotID()));
  }

  // 2. write slot
  page_handle->WriteSlot(rid.SlotID(), record.GetNullMap(), record.GetData(), true);

  // 3. unpin the page
  buffer_pool_manager_->UnpinPage(table_id_, rid.PageID(), true);
}

auto TableHandle::FetchPageHandle(page_id_t page_id) -> PageHandleUptr
{
 auto page = buffer_pool_manager_->FetchPage(table_id_, page_id);
 return WrapPageHandle(page);
}

auto TableHandle::CreatePageHandle() -> PageHandleUptr
{
 if (tab_hdr_.first_free_page_ == INVALID_PAGE_ID) {
   return CreateNewPageHandle();
 }
 auto page = buffer_pool_manager_->FetchPage(table_id_, tab_hdr_.first_free_page_);
 return WrapPageHandle(page);
}

auto TableHandle::CreateNewPageHandle() -> PageHandleUptr
{
 auto page_id = static_cast<page_id_t>(tab_hdr_.page_num_);
 tab_hdr_.page_num_++;
 auto page   = buffer_pool_manager_->FetchPage(table_id_, page_id);
 auto pg_hdl = WrapPageHandle(page);
 page->SetNextFreePageId(tab_hdr_.first_free_page_);
 tab_hdr_.first_free_page_ = page_id;
 return pg_hdl;
}

auto TableHandle::WrapPageHandle(Page* page) -> PageHandleUptr
{
 switch (storage_model_) {
   case StorageModel::NARY_MODEL: return std::make_unique<NAryPageHandle>(&tab_hdr_, page);
   case StorageModel::PAX_MODEL: return std::make_unique<PAXPageHandle>(&tab_hdr_, page, schema_.get(), field_offset_);
   default: WSDB_FETAL("Unknown storage model");
 }
}

auto TableHandle::GetTableId() const -> table_id_t { return table_id_; }

auto TableHandle::GetTableHeader() const -> const TableHeader& { return tab_hdr_; }

auto TableHandle::GetSchema() const -> const RecordSchema& { return *schema_; }

auto TableHandle::GetTableName() const -> std::string
{
 auto file_name = disk_manager_->GetFileName(table_id_);
 return OBJNAME_FROM_FILENAME(file_name);
}

auto TableHandle::GetStorageModel() const -> StorageModel { return storage_model_; }

auto TableHandle::GetFirstRID() -> RID
{
 auto page_id = FILE_HEADER_PAGE_ID + 1;
 while (page_id < static_cast<page_id_t>(tab_hdr_.page_num_)) {
   auto pg_hdl = FetchPageHandle(page_id);
   auto id = BitMap::FindFirst(pg_hdl->GetBitmap(), tab_hdr_.rec_per_page_, 0, true);
   if (id != tab_hdr_.rec_per_page_) {
     buffer_pool_manager_->UnpinPage(table_id_, page_id, false);
     return { page_id, static_cast<slot_id_t>(id) };
   }
   buffer_pool_manager_->UnpinPage(table_id_, page_id, false);
   page_id++;
 }
 return INVALID_RID;
}

auto TableHandle::GetNextRID(const RID& rid) -> RID
{
 auto page_id = rid.PageID();
 auto slot_id = rid.SlotID();
 while (page_id < static_cast<page_id_t>(tab_hdr_.page_num_)) {
   auto pg_hdl = FetchPageHandle(page_id);
   slot_id = static_cast<slot_id_t>(BitMap::FindFirst(pg_hdl->GetBitmap(), tab_hdr_.rec_per_page_, slot_id + 1, true));
   if (slot_id == static_cast<slot_id_t>(tab_hdr_.rec_per_page_)) {
     buffer_pool_manager_->UnpinPage(table_id_, page_id, false);
     page_id++;
     slot_id = -1;
   }
   else {
     buffer_pool_manager_->UnpinPage(table_id_, page_id, false);
     return { page_id, static_cast<slot_id_t>(slot_id) };
   }
 }
 return INVALID_RID;
}

auto TableHandle::HasField(const std::string& field_name) const -> bool
{
 return schema_->HasField(table_id_, field_name);
}

}  // namespace wsdb