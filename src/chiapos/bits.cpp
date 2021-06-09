#include "bits.hpp"

SmallVector::SmallVector() noexcept { count_ = 0; }

uint64_t& SmallVector::operator[](const uint16_t index) { return v_[index]; }

uint64_t SmallVector::operator[](const uint16_t index) const { return v_[index]; }

void SmallVector::push_back(uint64_t value) { v_[count_++] = value; }

SmallVector::size_type SmallVector::size() const noexcept { return count_; }

SmallVector& SmallVector::operator=(const SmallVector& other)
{
    memcpy(this, &other, sizeof(SmallVector));
    return (*this);
}

ParkVector::ParkVector() noexcept { count_ = 0; }

uint64_t& ParkVector::operator[](const uint32_t index) { return v_[index]; }

uint64_t ParkVector::operator[](const uint32_t index) const { return v_[index]; }

void ParkVector::push_back(uint64_t value) { v_[count_++] = value; }

ParkVector::size_type ParkVector::size() const noexcept { return count_; }

ParkVector& ParkVector::operator=(const ParkVector& other)
{
    count_ = other.count_;
    for (size_type i = 0; i < other.count_; i++) v_[i] = other.v_[i];
    return (*this);
}
