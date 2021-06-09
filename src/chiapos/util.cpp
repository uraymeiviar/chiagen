#include "util.hpp"

uint32_t Util::ByteAlign(uint32_t num_bits) { return (num_bits + (8 - ((num_bits) % 8)) % 8); }

std::string Util::HexStr(const uint8_t *data, size_t len)
{
	std::stringstream s;
	s << std::hex;
	for (size_t i = 0; i < len; ++i)
		s << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
	s << std::dec;
	return s.str();
}

void Util::IntToTwoBytes(uint8_t *result, const uint16_t input)
{
	uint16_t r = bswap_16(input);
	memcpy(result, &r, sizeof(r));
}

void Util::IntToTwoBytesLE(uint8_t *result, const uint16_t input)
{
	result[0] = input & 0xff;
	result[1] = input >> 8;
}

uint16_t Util::TwoBytesToInt(const uint8_t *bytes)
{
	uint16_t i;
	memcpy(&i, bytes, sizeof(i));
	return bswap_16(i);
}

void Util::IntToEightBytes(uint8_t *result, const uint64_t input)
{
	uint64_t r = bswap_64(input);
	memcpy(result, &r, sizeof(r));
}

void progress(int phase, int n, int max_n)
{
    float p = (100.0 / 4) * ((phase - 1.0) + (1.0 * n / max_n));
    std::cout << "Progress: " << p << std::endl;
}

void IntTo16Bytes(uint8_t *result, const uint128_t input)
{
	uint64_t r = bswap_64(input >> 64);
	memcpy(result, &r, sizeof(r));
	r = bswap_64((uint64_t)input);
	memcpy(result + 8, &r, sizeof(r));
}

uint64_t Util::EightBytesToInt(const uint8_t *bytes)
{
	uint64_t i;
	memcpy(&i, bytes, sizeof(i));
	return bswap_64(i);
}

void Util::IntTo16Bytes(uint8_t *result, const uint128_t input)
{
	uint64_t r = bswap_64(input >> 64);
	memcpy(result, &r, sizeof(r));
	r = bswap_64((uint64_t)input);
	memcpy(result + 8, &r, sizeof(r));
}

uint8_t Util::GetSizeBits(uint128_t value)
{
	uint8_t count = 0;
	while (value) {
		count++;
		value >>= 1;
	}
	return count;
}

uint64_t Util::SliceInt64FromBytes(
	const uint8_t *bytes,
	uint32_t start_bit,
	const uint32_t num_bits)
{
	uint64_t tmp;

	if (start_bit + num_bits > 64) {
		bytes += start_bit / 8;
		start_bit %= 8;
	}

	tmp = Util::EightBytesToInt(bytes);
	tmp <<= start_bit;
	tmp >>= 64 - num_bits;
	return tmp;
}

uint64_t Util::SliceInt64FromBytesFull(const uint8_t *bytes, uint32_t start_bit, uint32_t num_bits)
{
	uint32_t last_bit = start_bit + num_bits;
	uint64_t r = SliceInt64FromBytes(bytes, start_bit, num_bits);
	if (start_bit % 8 + num_bits > 64)
		r |= bytes[last_bit / 8] >> (8 - last_bit % 8);
	return r;
}

uint128_t Util::SliceInt128FromBytes(
	const uint8_t *bytes,
	const uint32_t start_bit,
	const uint32_t num_bits)
{
	if (num_bits <= 64)
		return SliceInt64FromBytesFull(bytes, start_bit, num_bits);

	uint32_t num_bits_high = num_bits - 64;
	uint64_t high = SliceInt64FromBytesFull(bytes, start_bit, num_bits_high);
	uint64_t low = SliceInt64FromBytesFull(bytes, start_bit + num_bits_high, 64);
	return ((uint128_t)high << 64) | low;
}

void Util::GetRandomBytes(uint8_t *buf, uint32_t num_bytes)
{
	std::random_device rd;
	std::mt19937 mt(rd());
	std::uniform_int_distribution<int> dist(0, 255);
	for (uint32_t i = 0; i < num_bytes; i++) {
		buf[i] = dist(mt);
	}
}

uint64_t Util::ExtractNum(
	const uint8_t *bytes,
	uint32_t len_bytes,
	uint32_t begin_bits,
	uint32_t take_bits)
{
	if ((begin_bits + take_bits) / 8 > len_bytes - 1) {
		take_bits = len_bytes * 8 - begin_bits;
	}
	return Util::SliceInt64FromBytes(bytes, begin_bits, take_bits);
}

uint64_t Util::RoundSize(uint64_t size)
{
	size *= 2;
	uint64_t result = 1;
	while (result < size) result *= 2;
	return result + 50;
}

int Util::MemCmpBits(uint8_t *left_arr, uint8_t *right_arr, uint32_t len, uint32_t bits_begin)
{
	uint32_t start_byte = bits_begin / 8;
	uint8_t mask = ((1 << (8 - (bits_begin % 8))) - 1);
	if ((left_arr[start_byte] & mask) != (right_arr[start_byte] & mask)) {
		return (left_arr[start_byte] & mask) - (right_arr[start_byte] & mask);
	}

	for (uint32_t i = start_byte + 1; i < len; i++) {
		if (left_arr[i] != right_arr[i])
			return left_arr[i] - right_arr[i];
	}
	return 0;
}

double Util::RoundPow2(double a)
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

void Util::CpuID(uint32_t leaf, uint32_t *regs)
{
	__cpuid((int *)regs, (int)leaf);
}

bool Util::HavePopcnt(void)
{
	// EAX, EBX, ECX, EDX
	uint32_t regs[4] = {0};

	CpuID(1, regs);
	// Bit 23 of ECX indicates POPCNT instruction support
	return (regs[2] >> 23) & 1;
}

uint64_t Util::PopCount(uint64_t n)
{
	return __popcnt64(n);
}

Timer::Timer()
{
	wall_clock_time_start_ = std::chrono::steady_clock::now();
	::GetProcessTimes(::GetCurrentProcess(), &ft_[3], &ft_[2], &ft_[1], &ft_[0]);
}

char *Timer::GetNow()
{
	auto now = std::chrono::system_clock::now();
	auto tt = std::chrono::system_clock::to_time_t(now);
	return ctime(&tt);  // ctime includes newline
}

void Timer::PrintElapsed(const std::string &name) const
{
	auto end = std::chrono::steady_clock::now();
	auto wall_clock_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
								end - this->wall_clock_time_start_)
								.count();

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


	double cpu_ratio = static_cast<int>(10000 * (cpu_time_ms / wall_clock_ms)) / 100.0;

	std::cout << name << " " << (wall_clock_ms / 1000.0) << " seconds. CPU (" << cpu_ratio
				<< "%) " << Timer::GetNow();
}

