#ifndef CLI_HPP
#define CLI_HPP

#include <string>
#include <filesystem>
#include <algorithm>

int cli_create(
	std::string farmer_key,
	std::string pool_key,
	std::filesystem::path finaldir = std::filesystem::path(),
	std::filesystem::path tempdir = std::filesystem::path(),
	std::filesystem::path tempdir2 = std::filesystem::path(), 
	std::wstring filename = L"",
	std::string memo = "",
	std::string id = "",
	uint8_t k = 32,
	uint32_t num_buckets = 128,
	uint32_t num_stripes = 65536,
	uint8_t num_threads = 2,
	uint32_t bufferSz = 4608,
	bool nobitfield = false
);
int cli_check(uint32_t iterations, std::wstring filename);
int cli_verify(std::string id, std::string proof, std::string challenge);
int cli_proof(std::string challenge, std::wstring filename);

std::wstring s2ws(const std::string& str);
std::string ws2s(const std::wstring& wstr);
void HexToBytes(const std::string &hex, uint8_t *result);
std::vector<unsigned char> intToBytes(uint32_t paramInt, uint32_t numBytes);
std::string hexStr(const std::vector<uint8_t>& data);
std::string Strip0x(const std::string &hex);

template <class T> T lowercase(const T& inp) {
	T data = inp;
	std::transform(data.begin(), data.end(), data.begin(),
	[](T::value_type c){ return std::tolower(c); });
	return data;
}

#endif