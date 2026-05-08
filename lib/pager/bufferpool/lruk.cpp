
#include "pager/bufferpool/evictor.h"

pager::LRUKEvictor::LRUKEvictor(size_t k, size_t num_frames) : k_(k) {
  node_store_.reserve(num_frames);
}

std::optional<pager::frame_id_t> pager::LRUKEvictor::evict() {
  if (evictable_size_ == 0)
    return std::nullopt;

  pager::frame_id_t victim;
  if (!less_than_k_set_.empty()) {
    auto it = less_than_k_set_.begin();
    victim = it->second;
    less_than_k_set_.erase(it);
  } else {
    auto it = k_set_.begin();
    victim = it->second;
    k_set_.erase(it);
  }

  node_store_.erase(victim);
  evictable_size_--;
  return victim;
}

void pager::LRUKEvictor::record_access(pager::frame_id_t frame_id) {
  size_t ts = current_timestamp_++;

  auto [it, is_new] = node_store_.emplace(frame_id, LRUKNode{frame_id});
  LRUKNode &node = it->second;

  bool was_less_than_k = node.history_.size() < k_;
  size_t old_front = node.history_.empty() ? 0 : node.history_.front();

  node.history_.push_back(ts);
  if (node.history_.size() > k_)
    node.history_.pop_front();

  bool is_less_than_k = node.history_.size() < k_;

  if (node.is_evictable_) {
    if (was_less_than_k && !is_less_than_k) {
      less_than_k_set_.erase({old_front, frame_id});
      k_set_.insert({node.history_.front(), frame_id});
    } else if (!was_less_than_k) {
      k_set_.erase({old_front, frame_id});
      k_set_.insert({node.history_.front(), frame_id});
    }
  }
}

void pager::LRUKEvictor::set_evictable(pager::frame_id_t frame_id,
                                        bool evictable) {
  auto it = node_store_.find(frame_id);
  if (it == node_store_.end())
    return;

  LRUKNode &node = it->second;
  if (node.is_evictable_ == evictable)
    return;

  bool is_less_than_k = node.history_.size() < k_;
  size_t key_ts = node.history_.empty() ? 0 : node.history_.front();

  if (evictable) {
    if (is_less_than_k)
      less_than_k_set_.insert({key_ts, frame_id});
    else
      k_set_.insert({key_ts, frame_id});
    evictable_size_++;
  } else {
    if (is_less_than_k)
      less_than_k_set_.erase({key_ts, frame_id});
    else
      k_set_.erase({key_ts, frame_id});
    evictable_size_--;
  }

  node.is_evictable_ = evictable;
}

void pager::LRUKEvictor::remove(pager::frame_id_t frame_id) {
  auto it = node_store_.find(frame_id);
  if (it == node_store_.end())
    return;

  LRUKNode &node = it->second;
  if (node.is_evictable_) {
    bool is_less_than_k = node.history_.size() < k_;
    size_t key_ts = node.history_.empty() ? 0 : node.history_.front();
    if (is_less_than_k)
      less_than_k_set_.erase({key_ts, frame_id});
    else
      k_set_.erase({key_ts, frame_id});
    evictable_size_--;
  }

  node_store_.erase(it);
}

size_t pager::LRUKEvictor::size() { return evictable_size_; }
