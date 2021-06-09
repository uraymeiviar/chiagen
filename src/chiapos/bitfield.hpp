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

#ifndef CHIAPOS_SRC_CPP_BITFIELD_HPP_
#define CHIAPOS_SRC_CPP_BITFIELD_HPP_

#include <memory>

class bitfield
{
public:
    explicit bitfield(int64_t size);
    void set(int64_t const bit);
    bool get(int64_t const bit) const;
    void clear();
    int64_t size() const;
    void swap(bitfield& rhs);
    int64_t count(int64_t const start_bit, int64_t const end_bit) const;
    void free_memory();
private:
    std::unique_ptr<uint64_t[]> buffer_;

    // number of 64-bit words
    int64_t size_;
};

#endif