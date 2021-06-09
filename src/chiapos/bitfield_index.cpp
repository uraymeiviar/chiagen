#include "bitfield_index.hpp"
#include <cassert>

bitfield_index::bitfield_index(bitfield const& b) : bitfield_(b)
{
    uint64_t counter = 0;
    index_.reserve(bitfield_.size() / kIndexBucket);

    for (int64_t idx = 0; idx < int64_t(bitfield_.size()); idx += kIndexBucket) {
        index_.push_back(counter);
        int64_t const left = std::min(int64_t(bitfield_.size()) - idx, kIndexBucket);
        counter += bitfield_.count(idx, idx + left);
    }
}

std::pair<uint64_t, uint64_t> bitfield_index::lookup(uint64_t pos, uint64_t offset) const
{
    uint64_t const bucket = pos / kIndexBucket;

    assert(bucket < index_.size());
    assert(pos < uint64_t(bitfield_.size()));
    assert(pos + offset < uint64_t(bitfield_.size()));
    assert(bitfield_.get(pos) && bitfield_.get(pos + offset));

    uint64_t const base = index_[bucket];

    int64_t const aligned_pos = pos & ~uint64_t(63);

    uint64_t const aligned_pos_count = bitfield_.count(bucket * kIndexBucket, aligned_pos);
    uint64_t const offset_count = aligned_pos_count + bitfield_.count(aligned_pos, pos + offset);
    uint64_t const pos_count = aligned_pos_count + bitfield_.count(aligned_pos, pos);

    assert(offset_count >= pos_count);

    return {base + pos_count, offset_count - pos_count};
}
