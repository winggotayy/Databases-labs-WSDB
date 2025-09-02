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
// Created by ziqi on 2024/7/17.
//
#include "buffer_pool_manager.h"
#include "replacer/lru_replacer.h"
#include "replacer/lru_k_replacer.h"

#include "../../../common/error.h"

namespace wsdb {

BufferPoolManager::BufferPoolManager(DiskManager* disk_manager, wsdb::LogManager* log_manager, size_t replacer_lru_k)
   : disk_manager_(disk_manager), log_manager_(log_manager)
{
 if (REPLACER == "LRUReplacer") {
   replacer_ = std::make_unique<LRUReplacer>();
 }
 else if (REPLACER == "LRUKReplacer") {
   replacer_ = std::make_unique<LRUKReplacer>(replacer_lru_k);
 }
 else {
   WSDB_FETAL("Unknown replacer: " + REPLACER);
 }
 // init free_list_
 for (frame_id_t i = 0; i < static_cast<frame_id_t>(BUFFER_POOL_SIZE); i++) {
   free_list_.push_back(i);
 }
}

auto BufferPoolManager::FetchPage(file_id_t fid, page_id_t pid) -> Page* {
  // WSDB_STUDENT_TODO(l1, t2);

  std::scoped_lock lock(latch_); // 自动管理锁的获取和释放，确保线程安全

  fid_pid_t page_key {fid, pid}; // 在哈希表中查找页面
  // 页面在buffer pool中
  if (auto iter = page_frame_lookup_.find(page_key); iter != page_frame_lookup_.end())
  {
    auto& frame = frames_[iter->second]; // 找到页面框架，获取对应的框架引用
    frame.Pin(); // 增加页面框架的引用计数，表示该页面正在被使用
    replacer_->Pin(iter->second); // 通知替换器页面被固定，以便替换器更新其内部状态
    return frame.GetPage();
  }
  // 页面不在buffer pool中
  frame_id_t frame_id = GetAvailableFrame(); // 获取一个可用的页面框架
  UpdateFrame(frame_id, fid, pid); // 更新页面框架的内容
  return frames_[frame_id].GetPage();
}

auto BufferPoolManager::UnpinPage(file_id_t fid, page_id_t pid, bool is_dirty) -> bool {
  // WSDB_STUDENT_TODO(l1, t2);

  std::scoped_lock lock(latch_);

  if (Frame* frame = GetFrame(fid, pid); frame && frame->GetPinCount() > 0) // 页面框架存在且引用计数大于0
  {
    frame->Unpin(); // 减少引用计数
    if (is_dirty)
    {
      frame->SetDirty(true);
      disk_manager_->WritePage(fid, pid, frame->GetPage()->GetData()); // 写回磁盘
    }
    // 通知替换器页面已解除固定
    replacer_->Unpin(static_cast<frame_id_t>(frame - frames_.data()));
    return true;
  }
  return false;
}

auto BufferPoolManager::DeletePage(file_id_t fid, page_id_t pid) -> bool {
  // WSDB_STUDENT_TODO(l1, t2);

  std::scoped_lock lock(latch_);

  auto it = page_frame_lookup_.find({fid, pid});
  if (it == page_frame_lookup_.end() || frames_[it->second].GetPinCount() > 0)
  {
    return false;
  }

  auto& frame = frames_[it->second];
  if (frame.IsDirty())
  {
    disk_manager_->WritePage(fid, pid, frame.GetPage()->GetData());
  }
  frame.Reset();  // 重置页面框架，清除其状态
  free_list_.push_back(it->second); // 将页面框架编号添加到自由列表中，表示该框架现在可用
  replacer_->Unpin(it->second); // 通知替换器该页面框架已被删除
  page_frame_lookup_.erase(it); // 从哈希表中删除对应的页面框架条目
  return true;
}

auto BufferPoolManager::DeleteAllPages(file_id_t fid) -> bool {
  // WSDB_STUDENT_TODO(l1, t2);

  std::scoped_lock lock(latch_);

  bool all_pages_deleted = true;

  for (auto iter = page_frame_lookup_.begin(); iter != page_frame_lookup_.end();)
  {
    if (iter->first.fid == fid)
    {
      auto& frame = frames_[iter->second];
      if (frame.GetPinCount() > 0)
      {
        all_pages_deleted = false;
        ++iter;
        continue;
      }
      if (frame.IsDirty())
      {
        disk_manager_->WritePage(fid, iter->first.pid, frame.GetPage()->GetData());
      }
      frame.Reset();
      free_list_.push_back(iter->second);
      replacer_->Unpin(iter->second);
      iter = page_frame_lookup_.erase(iter);
    }
    else
    {
      ++iter;
    }
  }

  return all_pages_deleted;
}

auto BufferPoolManager::FlushPage(file_id_t fid, page_id_t pid) -> bool {
  // WSDB_STUDENT_TODO(l1, t2);

  std::scoped_lock lock(latch_);

  if (Frame* frame = GetFrame(fid, pid); frame)
  {
    if (frame->IsDirty())
    {
      disk_manager_->WritePage(fid, pid, frame->GetPage()->GetData());
      frame->SetDirty(false);
    }
    return true;
  }
  return false;
}

auto BufferPoolManager::FlushAllPages(file_id_t fid) -> bool {
  // WSDB_STUDENT_TODO(l1, t2);

  std::scoped_lock lock(latch_);

  for (const auto& entry : page_frame_lookup_)
  {
    if (entry.first.fid == fid)
    {
      auto& frame = frames_[entry.second];
      if (frame.IsDirty())
      {
        disk_manager_->WritePage(fid, entry.first.pid, frame.GetPage()->GetData());
        frame.SetDirty(false);
      }
    }
  }
  return true;
}

auto BufferPoolManager::GetAvailableFrame() -> frame_id_t {
  // WSDB_STUDENT_TODO(l1, t2);

  // 列表不为空：直接从自由列表中获取框架
  if (!free_list_.empty())
  {
    frame_id_t frame_id = free_list_.front();
    free_list_.pop_front();
    return frame_id;
  }
  // 列表为空：从替换器中选择一个牺牲的页面框架
  frame_id_t victim_frame_id;
  // 调用替换器的 Victim 方法，请求一个牺牲的页面框架编号
  if (replacer_->Victim(&victim_frame_id))
  {
    for (const auto& entry : page_frame_lookup_)
    {
      if (entry.second == victim_frame_id)
      {
        auto& frame = frames_[victim_frame_id];
        if (frame.IsDirty())
        {
          disk_manager_->WritePage(entry.first.fid, entry.first.pid, frame.GetPage()->GetData());
        }
        page_frame_lookup_.erase(entry.first);
        return victim_frame_id;  // 返回牺牲的页面框架编号
      }
    }
  }

  WSDB_THROW(WSDB_NO_FREE_FRAME, "NO FREE FRAME!");
}

void BufferPoolManager::UpdateFrame(frame_id_t frame_id, file_id_t fid, page_id_t pid) {
  // WSDB_STUDENT_TODO(l1, t2);

  auto& frame = frames_[frame_id];
  //auto old_fid_pid = frame.GetPage()->GetFileIdPageId();
  // 旧页面条目
  file_id_t prev_fid = frame.GetPage()->GetFileId();
  page_id_t prev_pid = frame.GetPage()->GetPageId();

  if (frame.IsDirty())
  {
    disk_manager_->WritePage(prev_fid, prev_pid, frame.GetPage()->GetData());
  }

  page_frame_lookup_.erase({prev_fid, prev_pid});
  frame.Reset();
  disk_manager_->ReadPage(fid, pid, frame.GetPage()->GetData());
  frame.GetPage()->SetFilePageId(fid, pid);
  frame.Pin();
  replacer_->Pin(frame_id);
  page_frame_lookup_[{fid, pid}] = frame_id;
}

auto BufferPoolManager::GetFrame(file_id_t fid, page_id_t pid) -> Frame*
{
 const auto it = page_frame_lookup_.find({ fid, pid });
 return it == page_frame_lookup_.end() ? nullptr : &frames_[it->second];
}

}  // namespace wsdb