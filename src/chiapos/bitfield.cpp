#include "bitfield.hpp"
#include <cassert>
#include "util.hpp">

bitfield::bitfield(int64_t size) : buffer_(new uint64_t[(size + 63) / 64]), size_((size + 63) / 64)
{
    clear();
}

void bitfield::set(int64_t const bit)
{
    assert(bit / 64 < size_);
    buffer_[bit / 64] |= uint64_t(1) << (bit % 64);
}

bool bitfield::get(int64_t const bit) const
{
    assert(bit / 64 < size_);
    return (buffer_[bit / 64] & (uint64_t(1) << (bit % 64))) != 0;
}

void bitfield::clear() { std::memset(buffer_.get(), 0, size_ * 8); }

int64_t bitfield::size() const { return size_ * 64; }

void bitfield::swap(bitfield& rhs)
{
    using std::swap;
    swap(buffer_, rhs.buffer_);
    swap(size_, rhs.size_);
}

int64_t bitfield::count(int64_t const start_bit, int64_t const end_bit) const
{
    assert((start_bit % 64) == 0);
    assert(start_bit <= end_bit);

    uint64_t const* start = buffer_.get() + start_bit / 64;
    uint64_t const* end = buffer_.get() + end_bit / 64;
    int64_t ret = 0;
    while (start != end) {
        ret += Util::PopCount(*start);
        ++start;
    }
    int const tail = end_bit % 64;
    if (tail > 0) {
        uint64_t const mask = (uint64_t(1) << tail) - 1;
        ret += Util::PopCount(*end & mask);
    }
    return ret;
}

void bitfield::free_memory()
{
    buffer_.reset();
    size_ = 0;
}
