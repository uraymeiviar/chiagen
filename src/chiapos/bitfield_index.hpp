// Copyright 2020 Chia Network Inc

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//    http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CHIAPOS_SRC_CPP_BITFIELD_INDEX_HPP_
#define CHIAPOS_SRC_CPP_BITFIELD_INDEX_HPP_

#include <algorithm>
#include <vector>
#include "bitfield.hpp"

class bitfield_index
{
public:
    // Cache the number of set bits every kIndexBucket bits.
    // For a bitfield of size 2^32, this means a 32 MiB index
    static inline const int64_t kIndexBucket = 1024;
    bitfield_index(bitfield const& b);

    std::pair<uint64_t, uint64_t> lookup(uint64_t pos, uint64_t offset) const;
private:
    bitfield const& bitfield_;
    std::vector<uint64_t> index_;
};

#endif