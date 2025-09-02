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

#include "lru_k_replacer.h"
#include "common/config.h"
#include "../common/error.h"

using ULL = unsigned long long;

namespace wsdb {

LRUKReplacer::LRUKReplacer(size_t k) : max_size_(BUFFER_POOL_SIZE), k_(k) {
 cur_ts_ = 0;
 cur_size_ = 0;
}

auto LRUKReplacer::Victim(frame_id_t* frame_id) -> bool
{
  // WSDB_STUDENT_TODO(l1, f1);
  std::scoped_lock lock(latch_);

  // 没有可用的frame
  if (cur_size_ == 0)
  {
    return false;
  }

  // 1. 初始化变量
  ULL max_dist = 0; // 当前最大的“逆向K距离”
  frame_id_t selected_frame = -1; // 被选中的frame的ID
  timestamp_t earliest_ts = std::numeric_limits<timestamp_t>::max(); // 最早的时间戳（处理距离相同的情况）
  bool found_candidate = false;

  // 2. 查找具有最大“逆向K距离”的frame，记录候选frame
  for (frame_id_t i = 0; i < static_cast<frame_id_t>(max_size_); ++i)
  {
    // 当前frame是可以被淘汰的
    if (node_store_[i].IsEvictable())
    {
      ULL dist = node_store_[i].GetBackwardKDistance(cur_ts_); // 获取当前frame的逆向K距离

      if (dist > max_dist)
      {
        max_dist = dist;
        selected_frame = i;
        found_candidate = true;  // 找到一个候选frame
        earliest_ts = node_store_[i].FetchOldestTimestamp();  // 更新时间戳
      }
      else if (dist == max_dist)
      {
        // 进一步按时间戳选择
        timestamp_t frame_timestamp = node_store_[i].FetchOldestTimestamp();
        if (frame_timestamp < earliest_ts)
        {
          selected_frame = i;
          earliest_ts = frame_timestamp;
        }
      }
    }
  }

  // 3. 处理最大逆向K距离为+inf的情况
  if (max_dist == std::numeric_limits<ULL>::max())
  {
    // 表示有frame的逆向K距离不可计算，选择最早的时间戳
    for (frame_id_t i = 0; i < static_cast<frame_id_t>(max_size_); ++i)
    {
      if (node_store_[i].IsEvictable() && node_store_[i].GetBackwardKDistance(cur_ts_) == max_dist)
      {
        timestamp_t frame_timestamp = node_store_[i].FetchOldestTimestamp();
        if (frame_timestamp < earliest_ts)
        {
          selected_frame = i;
          earliest_ts = frame_timestamp;
        }
      }
    }
  }

  // 如果找到候选frame，则淘汰它
  if (found_candidate)
  {
    *frame_id = selected_frame;
    node_store_[*frame_id].ResetTimestampHistory();
    node_store_[*frame_id].SetEvictable(false);
    if (cur_size_ > 0)
      cur_size_--;
    return true;
  }

  return false;  // 如果没有找到合适的frame，返回false
}

void LRUKReplacer::Pin(frame_id_t frame_id)
{
  // WSDB_STUDENT_TODO(l1, f1);
  std::scoped_lock lock(latch_);

  // 查找 frame_id 是否已经存在于 node_store_
  auto node_itr = node_store_.find(frame_id);

  // 如果不存在，创建新的节点并插入
  if (node_itr == node_store_.end())
  {
    node_store_.emplace(frame_id, LRUKNode(frame_id, k_));  // 使用 emplace 构造对象并插入
    node_store_[frame_id].AddHistory(cur_ts_);  // 直接添加当前时间戳到历史记录
  }
  // 如果存在，更新节点历史记录
  else
  {
    LRUKNode& node = node_itr->second;
    node.AddHistory(cur_ts_);  // 添加当前时间戳到历史记录

    // 如果节点是可驱逐的，设置为不可驱逐并减少 cur_size_
    if (node.IsEvictable())
    {
      node.SetEvictable(false);
      if (cur_size_ > 0)
      {
        --cur_size_;
      }
    }
  }

  ++cur_ts_; // 递增时间戳，每次 Pin 操作更新时间
}

void LRUKReplacer::Unpin(frame_id_t frame_id)
{
  // WSDB_STUDENT_TODO(l1, f1);
  std::scoped_lock lock(latch_);

  // 查找 frame_id 是否在 node_store_ 中
  auto node_itr = node_store_.find(frame_id);

  // 如果找到了对应的节点
  if (node_itr != node_store_.end())
  {
    LRUKNode& node = node_itr->second;

    // 如果节点当前不可驱逐，标记为可驱逐并增加 cur_size_
    if (!node.IsEvictable())
    {
      node.SetEvictable(true);  // 标记为可驱逐
      ++cur_size_;              // 增加可驱逐的节点计数
    }
  }
}

auto LRUKReplacer::Size() -> size_t
{
  // WSDB_STUDENT_TODO(l1, f1);
  std::scoped_lock lock(latch_);

  return cur_size_;
}

}  // namespace wsdb