#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#pragma once

using TableID = uint64_t;
using PageNumber = uint64_t;
enum class ForkType { MAIN, FSM, VM };

struct PageKey {
  TableID tbl_id;
  PageNumber pgno;
  ForkType fork_type;

  PageKey(TableID tbl_id_, PageNumber pgno_) : tbl_id(tbl_id_), pgno(pgno_), fork_type(ForkType::MAIN) {};
  PageKey(TableID tbl_id_, PageNumber pgno_, ForkType fork_type_) : tbl_id(tbl_id_), pgno(pgno_), fork_type(fork_type_) {};
  PageKey() : tbl_id(0), pgno(0), fork_type(ForkType::MAIN) {};

  bool operator==(const PageKey &o) const {
    return tbl_id == o.tbl_id && pgno == o.pgno && fork_type == o.fork_type;
  }
  bool operator!=(const PageKey &o) const { return !(*this == o); }
  bool operator<(const PageKey &o) const {
    if (tbl_id != o.tbl_id) return tbl_id < o.tbl_id;
    if (pgno != o.pgno) return pgno < o.pgno;
    return fork_type < o.fork_type;
  }
};

const size_t PAGE_SIZE = 4096;
extern const char *DB_FILENAME;
const uint32_t CHECKSUM = 123456;

template <typename T> struct PagerKey {
  T value;
  static constexpr T max_value = std::numeric_limits<T>::max();
  static constexpr T min_value = std::numeric_limits<T>::min();

  PagerKey() = default;
  PagerKey(T v) : value(v) {}

  bool operator==(const PagerKey &o) const { return value == o.value; }
  bool operator!=(const PagerKey &o) const { return value != o.value; }
  bool operator<(const PagerKey &o) const { return value < o.value; }
  bool operator>(const PagerKey &o) const { return value > o.value; }
  bool operator<=(const PagerKey &o) const { return value <= o.value; }
  bool operator>=(const PagerKey &o) const { return value >= o.value; }

  PagerKey &operator++() {
    value++;
    return *this;
  };

  PagerKey operator++(int) {
    PagerKey temp = *this;
    ++(*this);
    return temp;
  }

  std::vector<std::byte> to_bytes() const {
    return {
        static_cast<std::byte>((value >> 24) & 0xFF),
        static_cast<std::byte>((value >> 16) & 0xFF),
        static_cast<std::byte>((value >> 8) & 0xFF),
        static_cast<std::byte>((value) & 0xFF),
    };
  }
};

using DefaultPagerKey = PagerKey<uint32_t>;

enum class StmtType { CREATE_TABLE, DROP_TABLE };
