#pragma once

#include "pager/bufferpool/bufferpool.h"
#include <list>
#include <optional>
#include <set>
#include <unordered_map>

namespace pager {

class Evictor {
public:
  virtual std::optional<pager::frame_id_t> evict() = 0;
  virtual void record_access(pager::frame_id_t frame_id) = 0;
  virtual void set_evictable(pager::frame_id_t frame_id, bool evictable) = 0;
  virtual void remove(pager::frame_id_t frame_id) = 0;
  virtual size_t size() = 0;
  virtual ~Evictor() = default;
};

struct LRUKNode {
  pager::frame_id_t frame_id_;
  std::list<size_t> history_;
  bool is_evictable_ = false;
};

class LRUKEvictor : public pager::Evictor {
private:
  size_t k_;
  size_t current_timestamp_ = 0;
  size_t evictable_size_ = 0;

  std::unordered_map<pager::frame_id_t, LRUKNode> node_store_;
  std::set<std::pair<size_t, pager::frame_id_t>> less_than_k_set_;
  std::set<std::pair<size_t, pager::frame_id_t>> k_set_;

public:
  LRUKEvictor(size_t k, size_t num_frames);

  std::optional<pager::frame_id_t> evict() override;
  void record_access(pager::frame_id_t frame_id) override;
  void set_evictable(pager::frame_id_t frame_id, bool evictable) override;
  void remove(pager::frame_id_t frame_id) override;
  size_t size() override;
};

} // namespace pager
