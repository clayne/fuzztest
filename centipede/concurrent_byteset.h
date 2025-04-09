// Copyright 2023 The Centipede Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This library defines the concepts "fuzzing feature" and "feature domain".
// It is used by Centipede, and it can be used by fuzz runners to
// define their features in a way most friendly to Centipede.
// Fuzz runners do not have to use this file nor to obey the rules defined here.
// But using this file and following its rules is the simplest way if you want
// Centipede to understand the details about the features generated by the
// runner.

#ifndef THIRD_PARTY_CENTIPEDE_CONCURRENT_BYTESET_H_
#define THIRD_PARTY_CENTIPEDE_CONCURRENT_BYTESET_H_

#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>

// WARNING!!!: Be very careful with what STL headers or other dependencies you
// add here. This header needs to remain mostly bare-bones so that we can
// include it into runner.

#include "absl/base/const_init.h"

namespace fuzztest::internal {

// TODO(kcc): replace the standalone ForEachNonZeroByte with code from here.
// TODO(kcc): ConcurrentByteSet is an unoptimized single-layer byte set.
// Implement multi-layer byte set(s).

// A fixed-size byte set containing kSize bytes, kSize must be a multiple of 64.
// Set() can be called concurrently with another Set(), other uses should be
// synchronized externally.
// Intended usage is to call ForEachNonZeroByte() from one thread.
//
// IMPORTANT!!! Objects of this class should only be constructed with static
// storage duration. This is because the class has intentionally uninitialized
// direct and transitive data members that rely on static initialization in the
// compiled process image.
template <size_t kSize>
class ConcurrentByteSet {
 public:
  static constexpr size_t kSizeInBytes = kSize;
  // kSize must be multiple of this.
  static constexpr size_t kSizeMultiple = 64;
  static_assert((kSize % kSizeMultiple) == 0);

  // Creates a ConcurrentByteSet with static storage duration.
  explicit constexpr ConcurrentByteSet(absl::ConstInitType) {}

  // Clears the set.
  void clear() { memset(bytes_, 0, sizeof(bytes_)); }

  // Sets element `idx` to `value`. `idx` must be <= kSize.
  // Can be called concurrently.
  void Set(size_t idx, uint8_t value) {
    if (idx >= kSize) __builtin_trap();
    __atomic_store_n(&bytes_[idx], value, __ATOMIC_RELAXED);
  }

  // Performs a saturated increment of element `idx`.
  void SaturatedIncrement(size_t idx) {
    if (idx >= kSize) __builtin_trap();
    uint8_t counter = __atomic_load_n(&bytes_[idx], __ATOMIC_RELAXED);
    if (counter != 255)
      __atomic_store_n(&bytes_[idx], counter + 1, __ATOMIC_RELAXED);
  }

  // Calls `action(index, value)` for every {index,value} of a non-zero byte in
  // the set, then sets all those bytes to zero.
  // `from` and `to` set the range of elements to iterate, both must be
  // multiples of kSizeMultiple.
  void ForEachNonZeroByte(const std::function<void(size_t, uint8_t)> &action,
                          size_t from = 0, size_t to = kSize) {
    using word_t = uintptr_t;
    constexpr size_t kWordSize = sizeof(word_t);
    if (from % kSizeMultiple) __builtin_trap();
    if (to % kSizeMultiple) __builtin_trap();
    if (to > kSize) __builtin_trap();
    // Iterate one word at a time.
    for (uint8_t *ptr = &bytes_[from], *end = &bytes_[to]; ptr < end;
         ptr += kWordSize) {
      word_t word;
      __builtin_memcpy(&word, ptr, kWordSize);
      if (!word) continue;
      __builtin_memset(ptr, 0, kWordSize);
      // This loop assumes little-endianness. (Tests will break on big-endian).
      for (size_t pos = 0; pos < kWordSize; pos++) {
        uint8_t value = word >> (pos * CHAR_BIT);  // lowest byte is taken.
        if (value) action(ptr - &bytes_[0] + pos, value);
      }
    }
  }

 private:
  // No initializer for performance (`kSize` can be quite large). Relies on
  // static initialization in the process image (see the class comment).
  uint8_t bytes_[kSize] __attribute__((aligned(64)));
};

// Similar to ConcurrentByteSet, but consists of two layers, upper and lower.
// The size of the lower layer is a multiple of the size of the upper layer.
// Set() writes 1 to an element in the upper layer and then writes `value` to an
// element of the lower value. This allows ForEachNonZeroByte() to
// skip sub-regions of lower layer that were not written to. Otherwise, the
// interface and the behaviour is equivalent to ConcurrentByteSet.
template <size_t kSize, typename Upper,
          typename Lower = ConcurrentByteSet<kSize>>
class LayeredConcurrentByteSet {
 public:
  static constexpr size_t kSizeInBytes = kSize;
  static constexpr size_t kSizeMultiple =
      Lower::kSizeMultiple * Upper::kSizeMultiple;
  static_assert(kSize == Lower::kSizeInBytes);

  LayeredConcurrentByteSet() = default;
  // Creates a LayeredConcurrentByteSet with static storage duration.
  explicit constexpr LayeredConcurrentByteSet(absl::ConstInitType)
      : upper_layer_(absl::kConstInit), lower_layer_(absl::kConstInit) {}

  void clear() {
    upper_layer_.clear();
    lower_layer_.clear();
  }

  void Set(size_t idx, uint8_t value) {
    if (idx >= kSize) __builtin_trap();
    upper_layer_.Set(idx / kLayerRatio, 1);
    lower_layer_.Set(idx, value);
  }

  void SaturatedIncrement(size_t idx) {
    if (idx >= kSize) __builtin_trap();
    upper_layer_.Set(idx / kLayerRatio, 1);
    lower_layer_.SaturatedIncrement(idx);
  }

  void ForEachNonZeroByte(const std::function<void(size_t, uint8_t)> &action,
                          size_t from = 0, size_t to = kSize) {
    if (to > kSize) __builtin_trap();
    if (from % kSizeMultiple) __builtin_trap();
    if (to % kSizeMultiple) __builtin_trap();
    size_t upper_from = from / kLayerRatio;
    size_t upper_to = to / kLayerRatio;
    upper_layer_.ForEachNonZeroByte(
        [&](size_t idx, uint8_t value) {
          size_t lower_from = idx * kLayerRatio;
          size_t lower_to = lower_from + kLayerRatio;
          lower_layer_.ForEachNonZeroByte(action, lower_from, lower_to);
        },
        upper_from, upper_to);
  }

 private:
  Upper upper_layer_;
  Lower lower_layer_;
  static constexpr size_t kLayerRatio =
      Lower::kSizeInBytes / Upper::kSizeInBytes;
  static_assert((Lower::kSizeInBytes % Upper::kSizeInBytes) == 0);
};

// Two-layer ConcurrentByteSet() with upper layer 64x smaller than the lower.
template <size_t kSize>
class TwoLayerConcurrentByteSet
    : public LayeredConcurrentByteSet<kSize, ConcurrentByteSet<kSize / 64>> {
 public:
  // Creates a TwoLayerConcurrentByteSet with static storage duration.
  explicit constexpr TwoLayerConcurrentByteSet(absl::ConstInitType)
      : LayeredConcurrentByteSet<kSize, ConcurrentByteSet<kSize / 64>>(
            absl::kConstInit) {}
};

}  // namespace fuzztest::internal

#endif  // THIRD_PARTY_CENTIPEDE_CONCURRENT_BYTESET_H_
