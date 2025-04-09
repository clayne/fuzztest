// Copyright 2022 The Centipede Authors.
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
//
// This library must not depend on anything other than libc so that fuzz targets
// using it doesn't gain redundant coverage. For the same reason this library
// uses raw __builtin_trap instead of CHECKs.
// We make an exception for <algorithm> for std::sort/std::unique,
// since <algorithm> is very lightweight.
// This library is also header-only, with all functions defined as inline.

#ifndef THIRD_PARTY_CENTIPEDE_FEATURE_H_
#define THIRD_PARTY_CENTIPEDE_FEATURE_H_

// WARNING!!!: Be very careful with what STL headers or other dependencies you
// add here. This header needs to remain mostly bare-bones so that we can
// include it into runner.
// <vector> is an exception, because it's too clumsy w/o it, and it introduces
// minimal code footprint.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace fuzztest::internal {

// Feature is an integer that identifies some unique behaviour
// of the fuzz target exercised by a given input.
// We say, this input has this feature with regard to this fuzz target.
// One example of a feature: a certain control flow edge being executed.
using feature_t = uint64_t;

// A vector of features. It is not expected to be ordered.
// It typically does not contain repetitions, but it's ok to have them.
using FeatureVec = std::vector<feature_t>;

namespace feature_domains {

// Feature domain is a subset of 64-bit integers dedicated to a certain
// kind of fuzzing features.
// All domains are of the same size (kDomainSize), This way, we can compute
// a domain for a given feature by dividing by kDomainSize.
class Domain {
 public:
  // kDomainSize is a large enough value to hold all PCs of our largest target.
  // It is also large enough to avoid too many collisions in other domains.
  // At the same time, it is small enough that all domains combined require
  // not too many bits (e.g. 32 bits is a good practical limit).
  // TODO(kcc): consider making feature_t a 32-bit type if we expect to not
  // use more than 32 bits.
  // NOTE: this value may change in future.
  static constexpr size_t kDomainSize = 1ULL << 27;

  constexpr Domain(size_t domain_id) : domain_id_(domain_id) {}

  constexpr feature_t begin() const { return kDomainSize * domain_id_; }
  constexpr feature_t end() const { return begin() + kDomainSize; }
  bool Contains(feature_t feature) const {
    return feature >= begin() && feature < end();
  }
  constexpr size_t domain_id() const { return domain_id_; }

  // Converts any `number` into a feature in this domain.
  feature_t ConvertToMe(size_t number) const {
    return begin() + number % kDomainSize;
  }

  // Returns the DomainId of the domain that the feature belongs to.
  static size_t FeatureToDomainId(feature_t feature) {
    return feature / kDomainSize;
  }

  // Returns the index into the domain of a feature.
  static size_t FeatureToIndexInDomain(feature_t feature) {
    return feature % kDomainSize;
  }

 private:
  const size_t domain_id_;
};

// Notes on Designing Features and Domains
//
// Abstractly, a "feature" signals that there was something interesting about
// the input that Centipede should keep investigating. After seeing a particular
// feature occur often enough, Centipede will become less interested.
//
// Generally, different types of features should be put in different domains.
// This is useful for two reasons. First, Centipede can display the feature
// count for each domain separately. Second, Centipede calculates features
// weights relative to the size of the domain. If two different types of
// features are squeezed into the same domain, an overabundance of one type of
// feature can cause the other type of feature to be undervalued.
//
// The number of features can fit inside a particular domain is finite (see
// kDomainSize). A feature outside that range will be mapped inside that range.
// If the space of all possible features is larger than kDomainSize, it is
// recommended that the feature value is hashed as it is calculated. Feature
// spaces typically have some sort of internal structure and mapping a
// structured feature space into kDomainSize via a modulus can create
// predictable aliasing. Hashing the feature value reduces the worst case effect
// of the feature aliasing. If hashing, it is also recommended that the domain
// is defined in such a way so that the number of features actually discovered
// in that domain stays below a fraction of kDomainSize, even if the number of
// possible features is huge. The more feature aliasing that occurs in practice,
// the less effective the domain.

// Catch-all domain for unknown features.
inline constexpr Domain kUnknown = {__COUNTER__};
static_assert(kUnknown.domain_id() == 0);  // No one used __COUNTER__ before.
// Represents PCs, i.e. control flow edges.
// Use ConvertPCFeatureToPcIndex() to convert back to a PC index.
inline constexpr Domain kPCs = {__COUNTER__};
static_assert(kPCs.domain_id() != kUnknown.domain_id());  // just in case.
// Features derived from edge counters. See Convert8bitCounterToNumber().
inline constexpr Domain k8bitCounters = {__COUNTER__};
// Features derived from data flow edges.
// A typical data flow edge is a pair of PCs: {store-PC, load-PC}.
// Another variant of a data flow edge is a pair of {global-address, load-PC}.
inline constexpr Domain kDataFlow = {__COUNTER__};
// Features derived from instrumenting CMP instructions. TODO(kcc): remove.
inline constexpr Domain kCMP = {__COUNTER__};
// Features in the following domains are created for comparison instructions
// 'a CMP b'. One component of the feature is the context, i.e. where the
// comparison happened. Another component depends on {a,b}.
//
// a == b.
// The other domains (kCMPModDiff, kCMPHamming, kCMPDiffLog) are for a != b.
inline constexpr Domain kCMPEq = {__COUNTER__};
// (a - b) if |a-b| < 32, see ABToCmpModDiff.
inline constexpr Domain kCMPModDiff = {__COUNTER__};
// hamming_distance(a, b), ABToCmpHamming.
inline constexpr Domain kCMPHamming = {__COUNTER__};
// log2(a > b ? a - b : b - a), see ABToCmpDiffLog.
inline constexpr Domain kCMPDiffLog = {__COUNTER__};
// A list of all the CMP domains.
inline constexpr std::array<Domain, 5> kCMPDomains = {{
    kCMP,
    kCMPEq,
    kCMPModDiff,
    kCMPHamming,
    kCMPDiffLog,
}};
// Features derived from observing function call stacks.
inline constexpr Domain kCallStack = {__COUNTER__};
// Features derived from computing (bounded) control flow paths.
inline constexpr Domain kBoundedPath = {__COUNTER__};
// Features derived from (unordered) pairs of PCs.
inline constexpr Domain kPCPair = {__COUNTER__};
// Features defined by a user via
// __attribute__((section("__centipede_extra_features"))).
// There is no hard guarantee how many user domains are available, feel free to
// add or remove domains as needed.
inline constexpr std::array<Domain, 16> kUserDomains = {{
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
    {__COUNTER__},
}};
// A fake domain, not actually used, must be last.
inline constexpr Domain kLastDomain = {__COUNTER__};
// For now, check that all domains (except maybe for kLastDomain) fit
// into 32 bits.
static_assert(kLastDomain.begin() <= (1ULL << 32));

inline constexpr size_t kNumDomains = kLastDomain.domain_id();

// Special feature used to indicate an absence of features. Typically used where
// a feature array must not be empty, but doesn't have any other features.
inline constexpr feature_t kNoFeature = kUnknown.begin();

}  // namespace feature_domains

// Converts an 8-bit coverage counter, i.e. a pair of {`pc_index`,
// `counter_value` must not be zero.
//
// We convert the 8-bit counter value to a number from 0 to 7
// by computing its binary log, i.e. 1=>0, 2=>1, 4=>2, 8=>3, ..., 128=>7.
// This is a heuristic, similar to that of AFL or libFuzzer
// that tries to encourage inputs with different number of repetitions
// of the same PC.
inline size_t Convert8bitCounterToNumber(size_t pc_index,
                                         uint8_t counter_value) {
  if (counter_value == 0) __builtin_trap();  // Wrong input.
  // Compute a log2 of counter_value, i.e. a value between 0 and 7.
  // __builtin_clz consumes a 32-bit integer.
  uint32_t counter_log2 =
      sizeof(uint32_t) * 8 - 1 - __builtin_clz(counter_value);
  return pc_index * 8 + counter_log2;
}

// Given the `feature` from the PC domain, returns the feature's
// pc_index. I.e. reverse of kPC.ConvertToMe(), assuming all PCs originally
// converted to features were less than Domain::kDomainSize.
inline size_t ConvertPCFeatureToPcIndex(feature_t feature) {
  auto domain = feature_domains::kPCs;
  if (!domain.Contains(feature)) __builtin_trap();
  return feature - domain.begin();
}

// Encodes {`pc1`, `pc2`} into a number.
// `pc1` and `pc2` are in range [0, `max_pc`)
inline size_t ConvertPcPairToNumber(uintptr_t pc1, uintptr_t pc2,
                                    uintptr_t max_pc) {
  return pc1 * max_pc + pc2;
}

// Transforms {a,b}, a!=b, into a number in [0,64) using a-b.
inline uintptr_t ABToCmpModDiff(uintptr_t a, uintptr_t b) {
  uintptr_t diff = a - b;
  return diff <= 32 ? diff : -diff < 32 ? 32 + -diff : 0;
}

// Transforms {a,b}, a!=b, into a number in [0,64) using hamming distance.
inline uintptr_t ABToCmpHamming(uintptr_t a, uintptr_t b) {
  return __builtin_popcountll(a ^ b) - 1;
}

// Transforms {a,b}, a!=b, into a number in [0,64) using log2(a-b).
inline uintptr_t ABToCmpDiffLog(uintptr_t a, uintptr_t b) {
  return __builtin_clzll(a > b ? a - b : b - a);
}

// A simple fixed-capacity array with push_back.
// Thread-compatible.
template <size_t kSize>
class FeatureArray {
 public:
  // Constructs an empty feature array.
  FeatureArray() = default;

  // pushes `feature` back if there is enough space.
  void push_back(feature_t feature) {
    if (num_features_ < kSize) {
      features_[num_features_++] = feature;
    }
  }

  // Makes the array empty.
  void clear() { num_features_ = 0; }

  // Returns the array's raw data.
  feature_t *data() { return &features_[0]; }

  // Returns the number of elements in the array.
  size_t size() const { return num_features_; }

 private:
  // NOTE: No initializer needed: object state is captured by `num_features_`.
  feature_t features_[kSize];
  size_t num_features_ = 0;
};

}  // namespace fuzztest::internal

#endif  // THIRD_PARTY_CENTIPEDE_FEATURE_H_
