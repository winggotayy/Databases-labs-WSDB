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

#include "lru_replacer.h"
#include "common/config.h"
#include "../common/error.h"
#include <algorithm>
namespace wsdb {

LRUReplacer::LRUReplacer() : cur_size_(0), max_size_(BUFFER_POOL_SIZE) {}

auto LRUReplacer::Victim(frame_id_t *frame_id) -> bool {
 //WSDB_STUDENT_TODO(l1, t1);

 // 锁定以保障线程安全
 std::scoped_lock lock(latch_);
 // 没有可淘汰的帧
 if (cur_size_ == 0 || lru_list_.empty())
 {
   return false;
 }
 // 找到链表中第一个可淘汰的帧
 for (auto it = lru_list_.begin(); it != lru_list_.end(); ++it)
 {
   if (it->second) // 可被淘汰
   {
     *frame_id = it->first;
   }
 }
 for (auto it = lru_list_.begin(); it != lru_list_.end(); ++it)
 {
   if (it->second) // 可被淘汰
   {
     *frame_id = it->first;
     lru_hash_.erase(it->first);
     lru_list_.erase(it);
     if (cur_size_ > 0)
     {
       --cur_size_;
     }
     return true;
   }
 }
 return false;  // 没找到可淘汰的帧
}

void LRUReplacer::Pin(frame_id_t frame_id) {
 //WSDB_STUDENT_TODO(l1, t1);

 std::scoped_lock lock(latch_);
 auto it = lru_hash_.find(frame_id);
 if (it != lru_hash_.end())
 {
   // 如果帧已经在链表中
   lru_list_.erase(it->second);  // 从旧位置移除
   lru_hash_.erase(it);
   if (cur_size_ > 0)
   {
     --cur_size_;
   }
 }
 // 添加到链表尾部并标记为不可淘汰
 lru_list_.emplace_back(frame_id, false);
 lru_hash_[frame_id] = std::prev(lru_list_.end());
}

void LRUReplacer::Unpin(frame_id_t frame_id) {
 //WSDB_STUDENT_TODO(l1, t1);

 std::scoped_lock lock(latch_);
 auto it = lru_hash_.find(frame_id);
 if (it != lru_hash_.end())
 {
   // 如果帧已经在链表中
   auto &entry = *(it->second);
   if (!entry.second) // 当前为固定状态
   {
     entry.second = true;  // 解除固定
     ++cur_size_;
   }
   return;
 }
 // 如果帧不在链表中，添加到链表尾部并标记为可淘汰
 lru_list_.emplace_back(frame_id, true);
 lru_hash_[frame_id] = std::prev(lru_list_.end());
 ++cur_size_;
}

auto LRUReplacer::Size() -> size_t {
 //WSDB_STUDENT_TODO(l1, t1);

 std::scoped_lock lock(latch_);
 return cur_size_;
}

}  // namespace wsdb
