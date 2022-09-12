
// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef FUZZTEST_FUZZTEST_INTERNAL_TABLE_OF_RECENT_COMPARES_H_
#define FUZZTEST_FUZZTEST_INTERNAL_TABLE_OF_RECENT_COMPARES_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/random/distributions.h"

namespace fuzztest::internal {

template <typename ContainerT>
struct DictionaryEntry {
  // Index to insert this entry.
  std::optional<size_t> position_hint;
  // Entry content.
  ContainerT value;
};

template <typename ContainerT>
bool operator==(const DictionaryEntry<ContainerT>& lhs,
                const DictionaryEntry<ContainerT>& rhs) {
  return lhs.position_hint == rhs.position_hint && lhs.value == rhs.value;
}

template <typename H, typename ContainerT>
H AbslHashValue(H h, const DictionaryEntry<ContainerT>& m) {
  return H::combine(std::move(h), m.position_hint, m.value);
}

template <typename PRNG>
size_t ChooseOffset(size_t size, PRNG& prng) {
  return absl::Uniform(prng, size_t{0}, size);
}

template <typename PRNG>
bool RandomBool(PRNG& prng) {
  return absl::Bernoulli(prng, 0.5);
}

// A fixed-sized table to store integer comparison arguments.
// Note that this is lossy as we randomly hash elements into a fixed
// table, without handling collisions.
template <class T>
class TableOfRecentCompares {
 public:
  // One page size.
  static_assert(std::is_integral_v<T> && (sizeof(T) == 1 || sizeof(T) == 2 ||
                                          sizeof(T) == 4 || sizeof(T) == 8),
                "TableOfRecentCompares only accepts basic types with size = "
                "{1, 2, 4, 8}.");
  // Use mask to calculate modulo, avoid % overflow.
  static constexpr size_t kTableSize = 4096U / sizeof(T);
  static constexpr T kValueMask = kTableSize - 1;

  // Use LCG algorithm generated pesudo random index.
  // https://en.wikipedia.org/wiki/Linear_congruential_generator
  void Insert(T lhs, T rhs) {
    insert_index_ = (insert_index_ * 37 + 89) & kValueMask;
    table_[insert_index_].lhs = lhs;
    table_[insert_index_].rhs = rhs;
  }

  // Returns values from the table that were compared to `val`.
  // `min` and `max` are the range limit for returned values.
  template <typename ValueType>
  std::vector<ValueType> GetMatchingIntegerDictionaryEntries(
      ValueType val, ValueType min = std::numeric_limits<ValueType>::min(),
      ValueType max = std::numeric_limits<ValueType>::max()) const {
    absl::flat_hash_set<T> dictionary_set = {};
    // Some simple optimization.
    if (min == std::numeric_limits<ValueType>::min() &&
        max == std::numeric_limits<ValueType>::max()) {
      for (int i = 0; i < kTableSize; ++i) {
        auto dict_entry = GetMatchingIntegerDictionaryEntry(val, i);
        if (dict_entry.has_value()) {
          dictionary_set.insert(std::move(*dict_entry));
        }
      }
    } else {
      for (int i = 0; i < kTableSize; ++i) {
        auto dict_entry = GetMatchingIntegerDictionaryEntry(val, i, min, max);
        if (dict_entry.has_value()) {
          dictionary_set.insert(std::move(*dict_entry));
        }
      }
    }
    return std::vector<ValueType>(dictionary_set.begin(), dictionary_set.end());
  }

  struct CompareEntry {
    T lhs;
    T rhs;
  };

  // Returns the matched other-side of a comparison pair:
  //    val = 5, for comparison pair [5, 10], returns 10.
  //    if no match, returns std::nullopt.
  template <typename ValueType>
  std::optional<ValueType> GetMatchingIntegerDictionaryEntry(ValueType val,
                                                             size_t idx) const {
    std::optional<ValueType> result = std::nullopt;
    if (static_cast<ValueType>(table_[idx].lhs) == val) {
      result = static_cast<ValueType>(table_[idx].rhs);
    } else if (static_cast<ValueType>(table_[idx].rhs) == val) {
      result = static_cast<ValueType>(table_[idx].lhs);
    }
    return result;
  }

  // Returns the matched other-side in [min, max] or std::nullopt.
  template <typename ValueType>
  std::optional<ValueType> GetMatchingIntegerDictionaryEntry(
      ValueType val, size_t idx, ValueType min, ValueType max) const {
    std::optional<ValueType> result =
        GetMatchingIntegerDictionaryEntry(val, idx);
    if (result.has_value()) {
      if (*result < min || *result > max) {
        result = std::nullopt;
      }
    }
    return result;
  }

  const std::array<CompareEntry, kTableSize>& GetTable() const {
    return table_;
  }

  template <typename PRNG>
  CompareEntry GetRandomEntry(PRNG& prng) const {
    return table_[ChooseOffset(kTableSize, prng)];
  }

  template <typename PRNG, typename ValueType>
  std::optional<ValueType> GetRandomSide(PRNG& prng, size_t idx, ValueType min,
                                         ValueType max) const {
    std::optional<ValueType> result = std::nullopt;
    const CompareEntry& entry = table_[idx];
    if (RandomBool(prng)) {
      ValueType val = static_cast<ValueType>(entry.lhs);
      if (min <= val && val <= max) {
        result = val;
      }
    } else {
      ValueType val = static_cast<ValueType>(entry.lhs);
      if (min <= val && val <= max) {
        result = val;
      }
    }
    return result;
  }

 private:
  // We compute the next index based on the previous one, so
  // we need to store it here.
  size_t insert_index_ = 0;
  std::array<CompareEntry, kTableSize> table_ = {};
};

// A fixed-sized table to store buffer comparison arguments.
// I.e., arguments of memcmp, strcmp, strncmp, etc. Note that this
// is lossy as we randomly hash elements into a fixed table, without handling
// collisions. The elements may also be corrupted when it's accessed
// concurrently, but the only effect is to make this table lossy and we
// can accept that. Making this atomic will make the sancov part too heavy
// and not worth it.
class TableOfRecentlyComparedBuffers {
  // Use mask to calculate modulo, avoid % overflow.
 public:
  static constexpr size_t kTableSize = 128;
  static constexpr size_t kValueMask = 127;
  static constexpr size_t kEntrySize = 128;

  // Use LCG algorithm generated pesudo random index.
  void Insert(const uint8_t* buf1, const uint8_t* buf2, size_t n) {
    // LCG algorithm parameter: (37, 89).
    insert_index_ = (insert_index_ * 37 + 89) & kValueMask;
    if (n >= kEntrySize) n = kEntrySize - 1;
    ComparedBufferEntry& entry = table_[insert_index_];
    entry.buf_size = n;
    std::copy(buf1, buf1 + n, entry.buf1.begin());
    std::copy(buf2, buf2 + n, entry.buf2.begin());
  }

  // Returns entries from the table that were compared to `val`.
  template <typename ContainerT>
  std::vector<DictionaryEntry<ContainerT>>
  GetMatchingContainerDictionaryEntries(const ContainerT& val) const {
    using T = typename ContainerT::value_type;
    static_assert(
        sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
        "GetMatchingDictionaryEntries only accepts basic"
        " types with size = {1, 2, 4, 8}.");
    absl::flat_hash_set<DictionaryEntry<ContainerT>> dictionary = {};
    for (const ComparedBufferEntry& table_entry : table_) {
      auto dict_entry = GetMatchingContainerDictionaryEntry(
          val, table_entry.buf1.data(), table_entry.buf2.data(),
          table_entry.buf_size);
      if (dict_entry.has_value()) {
        dictionary.insert(std::move(*dict_entry));
      }
    }
    return std::vector<DictionaryEntry<ContainerT>>(dictionary.begin(),
                                                    dictionary.end());
  }

  // Align the buffer to make sure deferencing reinterpret_cast buf1/buf2
  // can read the expected value.
  struct ComparedBufferEntry {
    size_t buf_size = 0;
    alignas(8) std::array<uint8_t, kEntrySize> buf1 = {};
    alignas(8) std::array<uint8_t, kEntrySize> buf2 = {};
  };

  template <typename ContainerT>
  static std::optional<DictionaryEntry<ContainerT>>
  GetMatchingContainerDictionaryEntry(const ContainerT& val,
                                      const uint8_t* buf1, const uint8_t* buf2,
                                      size_t buf_size) {
    using T = typename ContainerT::value_type;
    size_t val_size = val.size() * sizeof(T);
    static constexpr size_t kBufSizeValueMask = sizeof(T) - 1;

    // Filter out some impossible to match cases.
    if (((buf_size & kBufSizeValueMask) != 0) || val_size < buf_size ||
        buf_size == 0) {
      return std::nullopt;
    }
    auto offset1 =
        std::search(val.begin(), val.end(), reinterpret_cast<const T*>(buf1),
                    reinterpret_cast<const T*>(buf1 + buf_size));
    if (offset1 != val.end()) {
      return DictionaryEntry<ContainerT>{
          size_t(offset1 - val.begin()),
          MakeContainer<ContainerT>(buf2, buf_size)};
    } else {
      auto offset2 =
          std::search(val.begin(), val.end(), reinterpret_cast<const T*>(buf2),
                      reinterpret_cast<const T*>(buf2 + buf_size));
      if (offset2 != val.end()) {
        return DictionaryEntry<ContainerT>{
            size_t(offset2 - val.begin()),
            MakeContainer<ContainerT>(buf1, buf_size)};
      }
    }
    return std::nullopt;
  }

  template <typename PRNG>
  const ComparedBufferEntry& GetRandomEntry(PRNG& prng) const {
    return table_[ChooseOffset(kTableSize, prng)];
  }

  template <typename ContainerT, typename PRNG>
  static std::optional<DictionaryEntry<ContainerT>> GetRandomSide(
      PRNG& prng, const uint8_t* buf1, const uint8_t* buf2, size_t buf_size) {
    using T = typename ContainerT::value_type;
    static constexpr size_t kBufSizeValueMask = sizeof(T) - 1;
    if ((buf_size & kBufSizeValueMask) != 0 || buf_size == 0) {
      return std::nullopt;
    }
    if (RandomBool(prng)) {
      return DictionaryEntry<ContainerT>{
          std::nullopt, MakeContainer<ContainerT>(buf1, buf_size)};
    } else {
      return DictionaryEntry<ContainerT>{
          std::nullopt, MakeContainer<ContainerT>(buf2, buf_size)};
    }
  }

 private:
  template <typename ContainerT, typename T = typename ContainerT::value_type>
  static ContainerT MakeContainer(const uint8_t* buf, size_t buf_size) {
    ContainerT result = {};
    std::copy(reinterpret_cast<const T*>(buf),
              reinterpret_cast<const T*>(buf + buf_size),
              std::back_inserter(result));
    return result;
  }
  size_t insert_index_ = 0;
  std::array<ComparedBufferEntry, kTableSize> table_ = {};
};

class TablesOfRecentCompares {
 public:
  template <int I>
  const auto& Get() const {
    static_assert(I == 0 || I == 1 || I == 2 || I == 4 || I == 8,
                  "TablesOfRecentCompares::Get "
                  "should use I in {0, 1, 2, 4, 8}.");
    if constexpr (I == 0)
      return mem_cmp_table;
    else if constexpr (I == 1)
      return i8_cmp_table;
    else if constexpr (I == 2)
      return i16_cmp_table;
    else if constexpr (I == 4)
      return i32_cmp_table;
    else if constexpr (I == 8)
      return i64_cmp_table;
  }

  template <int I>
  auto& GetMutable() {
    static_assert(I == 0 || I == 1 || I == 2 || I == 4 || I == 8,
                  "TablesOfRecentCompares::GetMutable "
                  "should use I in {0, 1, 2, 4, 8}.");
    if constexpr (I == 0)
      return mem_cmp_table;
    else if constexpr (I == 1)
      return i8_cmp_table;
    else if constexpr (I == 2)
      return i16_cmp_table;
    else if constexpr (I == 4)
      return i32_cmp_table;
    else if constexpr (I == 8)
      return i64_cmp_table;
  }

 private:
  TableOfRecentCompares<uint8_t> i8_cmp_table = {};
  TableOfRecentCompares<uint16_t> i16_cmp_table = {};
  TableOfRecentCompares<uint32_t> i32_cmp_table = {};
  TableOfRecentCompares<uint64_t> i64_cmp_table = {};
  TableOfRecentlyComparedBuffers mem_cmp_table = {};
};

template <typename T>
class IntegerDictionary {
  static_assert(std::is_integral_v<T> && (sizeof(T) == 1 || sizeof(T) == 2 ||
                                          sizeof(T) == 4 || sizeof(T) == 8),
                "IntegerDictionary only accepts basic types with size = "
                "{1, 2, 4, 8}.");

 public:
  void MatchEntriesFromTableOfRecentCompares(
      T val, const TablesOfRecentCompares& torc,
      T min = std::numeric_limits<T>::min(),
      T max = std::numeric_limits<T>::max()) {
    dictionary_ = torc.Get<sizeof(T)>().GetMatchingIntegerDictionaryEntries(
        val, min, max);
  }
  void AddEntry(T val) { dictionary_.push_back(val); }
  bool IsEmpty() const { return dictionary_.empty(); }
  template <typename PRNG>
  T GetRandomSavedEntry(PRNG& prng) const {
    return dictionary_[ChooseOffset(dictionary_.size(), prng)];
  }

  template <typename PRNG>
  static std::optional<T> GetRandomTORCEntry(T val, PRNG& prng,
                                             const TablesOfRecentCompares& torc,
                                             T min, T max) {
    auto& table = torc.Get<sizeof(T)>();
    size_t random_offset = ChooseOffset(table.kTableSize, prng);
    std::optional<T> result =
        table.GetMatchingIntegerDictionaryEntry(val, random_offset, min, max);
    if (!result.has_value()) {
      result = table.GetRandomSide(prng, random_offset, min, max);
    }
    return result;
  }
  size_t Size() const { return dictionary_.size(); }

 private:
  std::vector<T> dictionary_ = {};
};

template <typename ContainerT>
class ContainerDictionary {
  static_assert(std::is_integral_v<typename ContainerT::value_type> &&
                    (sizeof(typename ContainerT::value_type) == 1 ||
                     sizeof(typename ContainerT::value_type) == 2 ||
                     sizeof(typename ContainerT::value_type) == 4 ||
                     sizeof(typename ContainerT::value_type) == 8),
                "ContainerDictionary only accepts container::value_type being "
                "basic types with size = "
                "{1, 2, 4, 8}.");

 public:
  void MatchEntriesFromTableOfRecentCompares(
      const ContainerT& val, const TablesOfRecentCompares& torc) {
    dictionary_ = torc.Get<0>().GetMatchingContainerDictionaryEntries(val);
    // we also try to find entries from TableOfRecentCompares.i32/i64.
    AddMatchingIntegerDictionaryEntriesFromTORC(val, torc);
  }

  bool IsEmpty() const { return dictionary_.empty(); }
  void AddEntry(DictionaryEntry<ContainerT>&& val) {
    dictionary_.push_back(std::move(val));
  }

  template <typename PRNG>
  const DictionaryEntry<ContainerT>& GetRandomSavedEntry(PRNG& prng) const {
    return dictionary_[ChooseOffset(dictionary_.size(), prng)];
  }

  template <typename PRNG>
  static std::optional<DictionaryEntry<ContainerT>> GetRandomTORCEntry(
      const ContainerT& val, PRNG& prng, const TablesOfRecentCompares& torc) {
    using T = typename ContainerT::value_type;
    std::optional<DictionaryEntry<ContainerT>> result = std::nullopt;
    // Get from mem_cmp_table or i*_cmp_table with 50/50 probability.
    if (RandomBool(prng)) {
      auto& memcmp_entry = torc.Get<0>().GetRandomEntry(prng);
      result =
          TableOfRecentlyComparedBuffers::GetMatchingContainerDictionaryEntry(
              val, memcmp_entry.buf1.data(), memcmp_entry.buf2.data(),
              memcmp_entry.buf_size);
      if (!result.has_value()) {
        result = TableOfRecentlyComparedBuffers::GetRandomSide<ContainerT>(
            prng, memcmp_entry.buf1.data(), memcmp_entry.buf2.data(),
            memcmp_entry.buf_size);
      }
    } else {
      if constexpr (sizeof(T) <= 4) {
        switch (absl::Uniform(prng, 0, 3)) {
          case 0: {
            auto i32_entry = torc.Get<4>().GetRandomEntry(prng);
            result = GetMatchingContainerDictionaryEntryFromInteger(
                val, i32_entry.lhs, i32_entry.rhs);
          } break;
          case 1: {
            // Somewhere in the past we observe that some implicit type
            // promotion will lead to no match found. So we try to cast 64bit
            // types to 32bit types and find matches. Demotion is explicit so we
            // do not consider that here.
            auto i64_entry = torc.Get<8>().GetRandomEntry(prng);
            result = GetMatchingContainerDictionaryEntryFromIntegerWithCastTo<
                uint32_t>(val, i64_entry.lhs, i64_entry.rhs);
          } break;
          case 2: {
            auto i64_entry = torc.Get<8>().GetRandomEntry(prng);
            result = GetMatchingContainerDictionaryEntryFromInteger(
                val, i64_entry.lhs, i64_entry.rhs);
          } break;
          default:
            break;
        }
      } else if constexpr (sizeof(T) <= 8) {
        auto i64_entry = torc.Get<8>().GetRandomEntry(prng);
        result = GetMatchingContainerDictionaryEntryFromInteger(
            val, i64_entry.lhs, i64_entry.rhs);
      }
    }
    return result;
  }
  size_t Size() const { return dictionary_.size(); }

 private:
  // Assuming the format and the system running this fuzzer has the
  // same endian.
  template <typename T>
  static std::optional<DictionaryEntry<ContainerT>>
  GetMatchingContainerDictionaryEntryFromInteger(const ContainerT& val, T lhs,
                                                 T rhs) {
    return TableOfRecentlyComparedBuffers::GetMatchingContainerDictionaryEntry(
        val, reinterpret_cast<const uint8_t*>(&lhs),
        reinterpret_cast<const uint8_t*>(&rhs), sizeof(T));
  }

  template <typename TCastTo, typename T>
  static std::optional<DictionaryEntry<ContainerT>>
  GetMatchingContainerDictionaryEntryFromIntegerWithCastTo(
      const ContainerT& val, T lhs, T rhs) {
    // Ignore overflows.
    TCastTo lhs_cast_to = static_cast<TCastTo>(lhs);
    TCastTo rhs_cast_to = static_cast<TCastTo>(rhs);
    return TableOfRecentlyComparedBuffers::GetMatchingContainerDictionaryEntry(
        val, reinterpret_cast<const uint8_t*>(&lhs_cast_to),
        reinterpret_cast<const uint8_t*>(&rhs_cast_to), sizeof(TCastTo));
  }

  // Cast integer types into byte arrays and use
  // `GetMatchingContainerDictionaryEntry` to find matches in `val`.
  void AddMatchingIntegerDictionaryEntriesFromTORC(
      const ContainerT& val, const TablesOfRecentCompares& torc) {
    using T = typename ContainerT::value_type;
    if constexpr (sizeof(T) <= 4) {
      if (val.size() >= 4) {
        for (auto& i : torc.Get<4>().GetTable()) {
          auto dict_entry_32 =
              GetMatchingContainerDictionaryEntryFromInteger(val, i.lhs, i.rhs);
          if (dict_entry_32.has_value()) {
            dictionary_.push_back(std::move(*dict_entry_32));
          }
        }
        for (auto& i : torc.Get<8>().GetTable()) {
          auto dict_entry_32 =
              GetMatchingContainerDictionaryEntryFromIntegerWithCastTo<
                  uint32_t>(val, i.lhs, i.rhs);
          if (dict_entry_32.has_value()) {
            dictionary_.push_back(std::move(*dict_entry_32));
          }
        }
      }
    }
    if constexpr (sizeof(T) <= 8) {
      if (val.size() >= 8) {
        for (auto& i : torc.Get<8>().GetTable()) {
          auto dict_entry_64 =
              GetMatchingContainerDictionaryEntryFromInteger(val, i.lhs, i.rhs);
          if (dict_entry_64.has_value()) {
            dictionary_.push_back(std::move(*dict_entry_64));
          }
        }
      }
    }
  }

  std::vector<DictionaryEntry<ContainerT>> dictionary_ = {};
};

}  // namespace fuzztest::internal

#endif  // FUZZTEST_FUZZTEST_INTERNAL_TABLE_OF_RECENT_COMPARES_H_
