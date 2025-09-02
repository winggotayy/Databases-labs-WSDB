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

#ifndef WSDB_LRU_K_REPLACER_H
#define WSDB_LRU_K_REPLACER_H
#include <list>
#include <mutex>
#include <unordered_map>
#include "replacer.h"
#include "../common/error.h"

namespace wsdb {

class LRUKReplacer : public Replacer
{
public:
 explicit LRUKReplacer(size_t k);

 ~LRUKReplacer() override = default;

 auto Victim(frame_id_t *frame_id) -> bool override;

 void Pin(frame_id_t frame_id) override;

 void Unpin(frame_id_t frame_id) override;

 auto Size() -> size_t override;

private:
 class LRUKNode
 {
 public:
   LRUKNode() = default;

   explicit LRUKNode(frame_id_t fid, size_t k) : fid_(fid), k(k), is_evictable_(false) {}

   void AddHistory(timestamp_t ts)
   {
     // WSDB_STUDENT_TODO(l1, f1);
     history_.push_back(ts);
     while (history_.size() > k)
     {
       history_.erase(history_.begin());
     }
   }

   /**
    * Get the distance between the current timestamp and the k-th timestamp in the history,
    * think: why return type is unsigned long long?
    * @param cur_ts
    * @return
    */
   auto GetBackwardKDistance(timestamp_t cur_ts) -> unsigned long long
   {
     // WSDB_STUDENT_TODO(l1, f1);
     if (history_.size() < k)
     {
       return std::numeric_limits<unsigned long long>::max(); // 历史列表中不足k个时间戳
     }

     size_t count = 0; // 用于计数，找到第k个时间戳
     for (auto it = history_.rbegin(); it != history_.rend() && count < k; ++it)
     {
       if (count + 1 == k)
       {
         // 找到第k个时间戳，计算与当前时间戳的差值
         return cur_ts - *it;
       }
       ++count;
     }
     return std::numeric_limits<unsigned long long>::max(); // 理论上不会执行到这里，因为前面已经检查过历史列表的大小
   }

   [[nodiscard]] auto IsEvictable() const -> bool
   {
     // WSDB_STUDENT_TODO(l1, f1);
     return is_evictable_;
   }

   auto SetEvictable(bool set_evictable) -> void
   {
     // WSDB_STUDENT_TODO(l1, f1);
     is_evictable_ = set_evictable;
   }

   // 自定义辅助函数
   auto FetchOldestTimestamp() const -> timestamp_t
   {
     if (!history_.empty())
       return *history_.begin();  // 使用迭代器获取第一个元素
     throw std::runtime_error("History is empty");  // 如果为空，抛出异常
   }

   auto ResetTimestampHistory() -> void
   {
     while (!history_.empty()) {
       history_.pop_front();  // 按顺序清空每个元素
     }
   }

 private:
   std::list<timestamp_t> history_;
   frame_id_t             fid_{INVALID_FRAME_ID};
   size_t                 k{};
   bool                   is_evictable_{};
 };

private:
 std::unordered_map<frame_id_t, LRUKNode> node_store_;  // frame_id -> LRUKNode
 size_t                                   cur_ts_{0};
 size_t                                   cur_size_{0};  // number of evictable frames
 size_t                                   max_size_;     // maximum number of frames that can be stored
 size_t                                   k_;            // k for LRU-k
 std::mutex                               latch_;        // mutex for curr_size_, node_store_, and curr_timestamp_
};
}  // namespace wsdb

#endif  // WSDB_LRU_K_REPLACER_H