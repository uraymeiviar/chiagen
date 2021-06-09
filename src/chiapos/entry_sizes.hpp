//
// Created by Mariano Sorgente on 2020/09/28.
//
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

#ifndef CHIAPOS_SRC_CPP_ENTRY_SIZES_HPP_
#define CHIAPOS_SRC_CPP_ENTRY_SIZES_HPP_
#include <math.h>
#include <stdio.h>
#define NOMINMAX

#include "calculate_bucket.hpp"
#include "pos_constants.hpp"
#include "util.hpp"

class EntrySizes {
public:
    static uint32_t GetMaxEntrySize(uint8_t k, uint8_t table_index, bool phase_1_size);
    // Get size of entries containing (sort_key, pos, offset). Such entries are
    // written to table 7 in phase 1 and to tables 2-7 in phase 2.
    static uint32_t GetKeyPosOffsetSize(uint8_t k);
    // Calculates the size of one C3 park. This will store bits for each f7 between
    // two C1 checkpoints, depending on how many times that f7 is present. For low
    // values of k, we need extra space to account for the additional variability.
    static uint32_t CalculateC3Size(uint8_t k);
    static uint32_t CalculateLinePointSize(uint8_t k);
    // This is the full size of the deltas section in a park. However, it will not be fully filled
    static uint32_t CalculateMaxDeltasSize(uint8_t k, uint8_t table_index);
    static uint32_t CalculateStubsSize(uint32_t k);
    static uint32_t CalculateParkSize(uint8_t k, uint8_t table_index);
};

#endif  // CHIAPOS_ENTRY_SIZES_HPP
