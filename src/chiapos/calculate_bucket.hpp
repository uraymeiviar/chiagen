// Copyright 2018 Chia Network Inc

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//    http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CHIAPOS_SRC_CPP_CALCULATE_BUCKET_HPP_
#define CHIAPOS_SRC_CPP_CALCULATE_BUCKET_HPP_

#include <stdint.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

#include "b3/blake3.h"
#include "bits.hpp"
#include "chacha8.h"
#include "pos_constants.hpp"
#include "util.hpp"

// ChaCha8 block size
const uint16_t kF1BlockSizeBits = 512;

// Extra bits of output from the f functions. Instead of being a function from k -> k bits,
// it's a function from k -> k + kExtraBits bits. This allows less collisions in matches.
// Refer to the paper for mathematical motivations.
const uint8_t kExtraBits = 6;

// Convenience variable
const uint8_t kExtraBitsPow = 1 << kExtraBits;

// B and C groups which constitute a bucket, or BC group. These groups determine how
// elements match with each other. Two elements must be in adjacent buckets to match.
const uint16_t kB = 119;
const uint16_t kC = 127;
const uint16_t kBC = kB * kC;

// This (times k) is the length of the metadata that must be kept for each entry. For example,
// for a table 4 entry, we must keep 4k additional bits for each entry, which is used to
// compute f5.
static const uint8_t kVectorLens[] = {0, 0, 1, 2, 4, 4, 3, 2};

// Class to evaluate F1
class F1Calculator {
public:
    F1Calculator() = default;
    F1Calculator(uint8_t k, const uint8_t* orig_key);
    ~F1Calculator();
    // Disable copying
    F1Calculator(const F1Calculator&) = delete;

    // Reloading the encryption key is a no-op since encryption state is local.
    inline void ReloadKey() {}
    // Performs one evaluation of the F function on input L of k bits.
    Bits CalculateF(const Bits& L) const;

    // Returns an evaluation of F1(L), and the metadata (L) that must be stored to evaluate F2.
    inline std::pair<Bits, Bits> CalculateBucket(const Bits& L) const
    {
        return std::make_pair(CalculateF(L), L);
    }

    // F1(x) values for x in range [first_x, first_x + n) are placed in res[].
    // n must not be more than 1 << kBatchSizes.
    void CalculateBuckets(uint64_t first_x, uint64_t n, uint64_t *res);

private:
    // Size of the plot
    uint8_t k_{};

    // ChaCha8 context
    struct chacha8_ctx enc_ctx_{};

    uint8_t *buf_{};
};

struct rmap_item {
    uint16_t count : 4;
    uint16_t pos : 12;
};

// Class to evaluate F2 .. F7.
class FxCalculator {
public:
    FxCalculator() = default;

    FxCalculator(uint8_t k, uint8_t table_index);
    inline ~FxCalculator() = default;
    // Disable copying
    FxCalculator(const FxCalculator&) = delete;
    inline void ReloadKey() {}
    // Performs one evaluation of the f function.
    std::pair<Bits, Bits> CalculateBucket(const Bits& y1, const Bits& L, const Bits& R) const;

    // Given two buckets with entries (y values), computes which y values match, and returns a list
    // of the pairs of indices into bucket_L and bucket_R. Indices l and r match iff:
    //   let  yl = bucket_L[l].y,  yr = bucket_R[r].y
    //
    //   For any 0 <= m < kExtraBitsPow:
    //   yl / kBC + 1 = yR / kBC   AND
    //   (yr % kBC) / kC - (yl % kBC) / kC = m   (mod kB)  AND
    //   (yr % kBC) % kC - (yl % kBC) % kC = (2m + (yl/kBC) % 2)^2   (mod kC)
    //
    // Instead of doing the naive algorithm, which is an O(kExtraBitsPow * N^2) comparisons on
    // bucket length, we can store all the R values and lookup each of our 32 candidates to see if
    // any R value matches. This function can be further optimized by removing the inner loop, and
    // being more careful with memory allocation.
    int32_t FindMatches(
        const std::vector<PlotEntry>& bucket_L,
        const std::vector<PlotEntry>& bucket_R,
        uint16_t *idx_L,
        uint16_t *idx_R);

private:
    uint8_t k_{};
    uint8_t table_index_{};
    std::vector<struct rmap_item> rmap;
    std::vector<size_t> rmap_clean;

	static void load_tables();
	static uint16_t L_targets[2][kBC][kExtraBitsPow];
	static bool initialized;
};

#endif  // SRC_CPP_CALCULATE_BUCKET_HPP_
