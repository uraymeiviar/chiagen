#ifndef CLI_HPP
#define CLI_HPP

#include <string>
#include <filesystem>

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
int cli_create_mad(
	std::string farmer_key,
	std::string pool_key,
	std::filesystem::path finaldir = std::filesystem::path(),
	std::filesystem::path tempdir = std::filesystem::path(),
	std::filesystem::path tempdir2 = std::filesystem::path(), 
	uint32_t num_buckets = 128,
	uint8_t num_threads = 2
);

#endif