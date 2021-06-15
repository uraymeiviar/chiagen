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

#ifndef SRC_CPP_UTIL_HPP_
#define SRC_CPP_UTIL_HPP_

#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <assert.h>
#include <algorithm>
#include "stdiox.hpp"

template <typename Int>
constexpr inline Int cdiv(Int a, int b) { return (a + b - 1) / b; }

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <processthreadsapi.h>
#include "uint128_t.h"
#else
// __uint__128_t is only available in 64 bit architectures and on certain
// compilers.
typedef __uint128_t uint128_t;

// Allows printing of uint128_t
std::ostream &operator<<(std::ostream &strm, uint128_t const &v)
{
    strm << "uint128(" << (uint64_t)(v >> 64) << "," << (uint64_t)(v & (((uint128_t)1 << 64) - 1))
         << ")";
    return strm;
}

#endif

// compiler-specific byte swap macros.
#if defined(_MSC_VER)

#include <cstdlib>

// https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/byteswap-uint64-byteswap-ulong-byteswap-ushort?view=msvc-160
inline uint16_t bswap_16(uint16_t x) { return _byteswap_ushort(x); }
inline uint32_t bswap_32(uint32_t x) { return _byteswap_ulong(x); }
inline uint64_t bswap_64(uint64_t x) { return _byteswap_uint64(x); }

#elif defined(__clang__) || defined(__GNUC__)

inline uint16_t bswap_16(uint16_t x) { return __builtin_bswap16(x); }
inline uint32_t bswap_32(uint32_t x) { return __builtin_bswap32(x); }
inline uint64_t bswap_64(uint64_t x) { return __builtin_bswap64(x); }

#else
#error "unknown compiler, don't know how to swap bytes"
#endif

#if !defined(_MSC_VER)
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#else
#define likely(x)       x
#define unlikely(x)     x
#endif

/* Platform-specific cpuid include. */
#if defined(_WIN32)
#include <intrin.h>
#elif defined(__x86_64__)
#include <cpuid.h>
#endif

class Timer {
public:
    Timer()
    {
        wall_clock_time_start_ = std::chrono::steady_clock::now();
#if _WIN32
        ::GetProcessTimes(::GetCurrentProcess(), &ft_[3], &ft_[2], &ft_[1], &ft_[0]);
#else
        cpu_time_start_ = clock();
#endif
    }

    static char *GetNow()
    {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        return ctime(&tt);  // ctime includes newline
    }

    void PrintElapsed(const std::string &name) const
    {
        auto end = std::chrono::steady_clock::now();
        auto wall_clock_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 end - this->wall_clock_time_start_)
                                 .count();

#if _WIN32
        FILETIME nowft_[6];
        nowft_[0] = ft_[0];
        nowft_[1] = ft_[1];

        ::GetProcessTimes(::GetCurrentProcess(), &nowft_[5], &nowft_[4], &nowft_[3], &nowft_[2]);
        ULARGE_INTEGER u[4];
        for (size_t i = 0; i < 4; ++i) {
            u[i].LowPart = nowft_[i].dwLowDateTime;
            u[i].HighPart = nowft_[i].dwHighDateTime;
        }
        double user = (u[2].QuadPart - u[0].QuadPart) / 10000.0;
        double kernel = (u[3].QuadPart - u[1].QuadPart) / 10000.0;
        double cpu_time_ms = user + kernel;
#else
        double cpu_time_ms =
            1000.0 * (static_cast<double>(clock()) - this->cpu_time_start_) / CLOCKS_PER_SEC;
#endif

        double cpu_ratio = static_cast<int>(10000 * (cpu_time_ms / wall_clock_ms)) / 100.0;

        std::cout << name << " " << (wall_clock_ms / 1000.0) << " seconds. CPU (" << cpu_ratio
                  << "%) " << Timer::GetNow();
    }

private:
    std::chrono::time_point<std::chrono::steady_clock> wall_clock_time_start_;
#if _WIN32
    FILETIME ft_[4];
#else
    clock_t cpu_time_start_;
#endif

};

template <typename X>
inline X Mod(X i, X n)
{
    return (i % n + n) % n;
}

inline uint32_t ByteAlign(uint32_t num_bits) { return (num_bits + (8 - ((num_bits) % 8)) % 8); }

inline std::string HexStr(const uint8_t *data, size_t len)
{
    std::stringstream s;
    s << std::hex;
    for (size_t i = 0; i < len; ++i)
        s << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    s << std::dec;
    return s.str();
}

inline void IntToTwoBytes(uint8_t *result, const uint16_t input)
{
    uint16_t r = bswap_16(input);
    memcpy(result, &r, sizeof(r));
}

// Used to encode deltas object size
inline void IntToTwoBytesLE(uint8_t *result, const uint16_t input)
{
    result[0] = input & 0xff;
    result[1] = input >> 8;
}

inline uint16_t TwoBytesToInt(const uint8_t *bytes)
{
    uint16_t i;
    memcpy(&i, bytes, sizeof(i));
    return bswap_16(i);
}

/*
    * Converts a 64 bit int to bytes.
    */
inline void IntToEightBytes(uint8_t *result, const uint64_t input)
{
    uint64_t r = bswap_64(input);
    memcpy(result, &r, sizeof(r));
}

/*
    * Converts a byte array to a 64 bit int.
    */
inline uint64_t EightBytesToInt(const uint8_t *bytes)
{
    uint64_t i;
    memcpy(&i, bytes, sizeof(i));
    return bswap_64(i);
}

static void IntTo16Bytes(uint8_t *result, const uint128_t input)
{
    uint64_t r = bswap_64(input >> 64);
    memcpy(result, &r, sizeof(r));
    r = bswap_64((uint64_t)input);
    memcpy(result + 8, &r, sizeof(r));
}

/*
    * Retrieves the size of an integer, in Bits.
    */
inline uint8_t GetSizeBits(uint128_t value)
{
    uint8_t count = 0;
    while (value) {
        count++;
        value >>= 1;
    }
    return count;
}

// 'bytes' points to a big-endian 64 bit value (possibly truncated, if
// (start_bit % 8 + num_bits > 64)). Returns the integer that starts at
// 'start_bit' that is 'num_bits' long (as a native-endian integer).
//
// Note: requires that 8 bytes after the first sliced byte are addressable
// (regardless of 'num_bits'). In practice it can be ensured by allocating
// extra 7 bytes to all memory buffers passed to this function.
inline uint64_t SliceInt64FromBytes(
    const uint8_t *bytes,
    size_t start_bit,
    const size_t num_bits)
{
    uint64_t tmp;

    if (start_bit + num_bits > 64) {
        bytes += start_bit / 8;
        start_bit %= 8;
    }

    tmp = EightBytesToInt(bytes);
    tmp <<= start_bit;
    tmp >>= 64 - num_bits;
    return tmp;
}

inline uint64_t SliceInt64FromBytesFull(
    const uint8_t *bytes,
    size_t start_bit,
    size_t num_bits)
{
    size_t last_bit = start_bit + num_bits;
    uint64_t r = SliceInt64FromBytes(bytes, start_bit, num_bits);
    if (start_bit % 8 + num_bits > 64)
        r |= bytes[last_bit / 8] >> (8 - last_bit % 8);
    return r;
}

inline uint128_t SliceInt128FromBytes(
    const uint8_t *bytes,
    const size_t start_bit,
    const size_t num_bits)
{
    if (num_bits <= 64)
        return SliceInt64FromBytesFull(bytes, start_bit, num_bits);

    size_t num_bits_high = num_bits - 64;
    uint64_t high = SliceInt64FromBytesFull(bytes, start_bit, num_bits_high);
    uint64_t low = SliceInt64FromBytesFull(bytes, start_bit + num_bits_high, 64);
    return ((uint128_t)high << 64) | low;
}

inline void GetRandomBytes(uint8_t *buf, size_t num_bytes)
{
    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    for (size_t i = 0; i < num_bytes; i++) {
        buf[i] = dist(mt);
    }
}

inline uint64_t ExtractNum(
    const uint8_t *bytes,
    size_t len_bytes,
    size_t begin_bits,
    size_t take_bits)
{
    if ((begin_bits + take_bits) / 8 > len_bytes - 1) {
        take_bits = len_bytes * 8 - begin_bits;
    }
    return SliceInt64FromBytes(bytes, begin_bits, take_bits);
}

// The number of memory entries required to do the custom SortInMemory algorithm, given the
// total number of entries to be sorted.
inline uint64_t RoundSize(uint64_t size)
{
    size *= 2;
    uint64_t result = 1;
    while (result < size) result *= 2;
    return result + 50;
}

/*
    * Like memcmp, but only compares starting at a certain bit.
    */
inline int MemCmpBits(
    uint8_t *left_arr,
    uint8_t *right_arr,
    size_t len,
    size_t bits_begin)
{
    size_t start_byte = bits_begin / 8;
    uint8_t mask = ((1 << (8 - (bits_begin % 8))) - 1);
    if ((left_arr[start_byte] & mask) != (right_arr[start_byte] & mask)) {
        return (left_arr[start_byte] & mask) - (right_arr[start_byte] & mask);
    }

    for (size_t i = start_byte + 1; i < len; i++) {
        if (left_arr[i] != right_arr[i])
            return left_arr[i] - right_arr[i];
    }
    return 0;
}

inline double RoundPow2(double a)
{
    // https://stackoverflow.com/questions/54611562/truncate-float-to-nearest-power-of-2-in-c-performance
    int exp;
    double frac = frexp(a, &exp);
    if (frac > 0.0)
        frac = 0.5;
    else if (frac < 0.0)
        frac = -0.5;
    double b = ldexp(frac, exp);
    return b;
}

#if defined(_WIN32) || defined(__x86_64__)
inline void CpuID(uint32_t leaf, uint32_t *regs)
{
#if defined(_WIN32)
    __cpuid((int *)regs, (int)leaf);
#else
    __get_cpuid(leaf, &regs[0], &regs[1], &regs[2], &regs[3]);
#endif /* defined(_WIN32) */
}

inline bool HavePopcnt(void)
{
    // EAX, EBX, ECX, EDX
    uint32_t regs[4] = {0};

    CpuID(1, regs);
    // Bit 23 of ECX indicates POPCNT instruction support
    return (regs[2] >> 23) & 1;
}
#endif /* defined(_WIN32) || defined(__x86_64__) */

inline uint64_t PopCount(uint64_t n)
{
#if defined(_WIN32)
    return __popcnt64(n);
#elif defined(__x86_64__)
    uint64_t r;
    __asm__("popcnt %1, %0" : "=r"(r) : "r"(n));
    return r;
#else
    return __builtin_popcountl(n);
#endif /* defined(_WIN32) ... defined(__x86_64__) */
}


inline
int64_t get_wall_time_micros() {
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline
std::vector<uint8_t> hex_to_bytes(const std::string& hex)
{
	std::vector<uint8_t> result;
	for(size_t i = 0; i < hex.length(); i += 2) {
		const std::string byteString = hex.substr(i, 2);
		result.push_back((uint8_t)::strtol(byteString.c_str(), NULL, 16));
	}
	return result;
}

inline
std::string get_date_string_ex(const char* format, bool UTC = false, int64_t time_secs = -1) {
	::time_t time_;
	if(time_secs < 0) {
		::time(&time_);
	} else {
		time_ = time_secs;
	}
	::tm* tmp;
	if(UTC) {
		tmp = ::gmtime(&time_);
	} else {
		tmp = ::localtime(&time_);
	}
	char buf[256];
	::strftime(buf, sizeof(buf), format, tmp);
	return std::string(buf);
}

inline std::string systemClockToStr(std::chrono::time_point<std::chrono::system_clock> st) {
	std::time_t ct = std::chrono::system_clock::to_time_t(st);
	std::tm* t = std::localtime(&ct);
	char buf[256];
	std::strftime(buf,256,"%d/%m %H:%M:%S",t);
	buf[255] = '\0';
	return std::string(buf);
}

inline
std::ifstream::pos_type get_file_size(const wchar_t* file_name)
{
	std::ifstream in(file_name, std::ifstream::ate | std::ifstream::binary);
	return in.tellg(); 
}

inline
void fseek_set(FILE* file, uint64_t offset) {
	if(FSEEK(file, offset, SEEK_SET)) {
		throw std::runtime_error("fseek() failed");
	}
}

inline
size_t fwrite_ex(FILE* file, const void* buf, size_t length) {
	if(fwrite(buf, 1, length, file) != length) {
		throw std::runtime_error("fwrite() failed");
	}
	return length;
}

inline
size_t fwrite_at(FILE* file, uint64_t offset, const void* buf, size_t length) {
	fseek_set(file, offset);
	fwrite_ex(file, buf, length);
	return length;
}

inline
void remove(const std::string& file_name) {
	std::remove(file_name.c_str());
}

inline std::wstring s2ws(const std::string& str)
{
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;
	return converterX.from_bytes(str);
}

inline std::string ws2s(const std::wstring& wstr)
{
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;
	return converterX.to_bytes(wstr);
}

inline void HexToBytes(const std::string &hex, uint8_t *result)
{
	for (uint32_t i = 0; i < hex.length(); i += 2) {
		std::string byteString = hex.substr(i, 2);
		uint8_t byte = (uint8_t)strtol(byteString.c_str(), NULL, 16);
		result[i / 2] = byte;
	}
}

inline std::vector<unsigned char> intToBytes(uint32_t paramInt, uint32_t numBytes)
{
	std::vector<unsigned char> arrayOfByte(numBytes, 0);
	for (uint32_t i = 0; paramInt > 0; i++) {
		arrayOfByte[numBytes - i - 1] = paramInt & 0xff;
		paramInt >>= 8;
	}
	return arrayOfByte;
}

inline std::string Strip0x(const std::string &hex)
{
	if (hex.size() > 1 && (hex.substr(0, 2) == "0x" || hex.substr(0, 2) == "0X")) {
		return hex.substr(2);
	}
	return hex;
}

inline std::string hexStr(const std::vector<uint8_t>& data)
{
	 std::stringstream ss;
	 ss << std::hex;

	 for( int i(0) ; i < data.size(); ++i )
		 ss << std::setw(2) << std::setfill('0') << (int)data[i];

	 return ss.str();
}

template <class T> T lowercase(const T& inp) {
	T data = inp;
	std::transform(data.begin(), data.end(), data.begin(),
	[](T::value_type c){ return std::tolower(c); });
	return data;
}

inline std::string strRemoveChars(const std::string& inp, const std::string& chars) {
	std::string result = inp;
	result.erase(
		std::remove_if(
			result.begin(), 
			result.end(),
			[=](char c) {
				for (const auto tc : chars) {
					if (tc == c) {
						return true;
					}
				}
				return false;
			}
		),
		result.end()
	);
	return result;
}

inline std::string strFilterHexStr(const std::string& inp) {
	std::string result = inp;
	if (result.substr(0, 2) == "0x" || result.substr(0, 2) == "0X") {
		result = inp.substr(2);
	}
	result.erase(
		std::remove_if(
			result.begin(), 
			result.end(),
			[=](char c) {
				if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
					return false;
				}
				else {
					return true;
				}
			}
		),
		result.end()
	);
	return result;
}

#endif  // SRC_CPP_UTIL_HPP_
