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

#include <ctime>
#include <set>

#include "chiapos/picosha2.hpp"
#include "chiapos/plotter_disk.hpp"
#include "chiapos/prover_disk.hpp"
#include "chiapos/verifier.hpp"
#include "chiapos/thread_pool.hpp"
#include <ctime>
#include <iomanip>
#include <codecvt>
#include "Keygen.hpp"

using std::string;
using std::wstring;
using std::vector;
using std::endl;
using std::cout;
using std::wcout;


std::wstring s2ws(const std::string& str)
{
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;
	return converterX.from_bytes(str);
}

std::string ws2s(const std::wstring& wstr)
{
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;
	return converterX.to_bytes(wstr);
}

void HexToBytes(const string &hex, uint8_t *result)
{
	for (uint32_t i = 0; i < hex.length(); i += 2) {
		string byteString = hex.substr(i, 2);
		uint8_t byte = (uint8_t)strtol(byteString.c_str(), NULL, 16);
		result[i / 2] = byte;
	}
}

vector<unsigned char> intToBytes(uint32_t paramInt, uint32_t numBytes)
{
	vector<unsigned char> arrayOfByte(numBytes, 0);
	for (uint32_t i = 0; paramInt > 0; i++) {
		arrayOfByte[numBytes - i - 1] = paramInt & 0xff;
		paramInt >>= 8;
	}
	return arrayOfByte;
}

string Strip0x(const string &hex)
{
	if (hex.size() > 1 && (hex.substr(0, 2) == "0x" || hex.substr(0, 2) == "0X")) {
		return hex.substr(2);
	}
	return hex;
}

string hexStr(const std::vector<uint8_t>& data)
{
	 std::stringstream ss;
	 ss << std::hex;

	 for( int i(0) ; i < data.size(); ++i )
		 ss << std::setw(2) << std::setfill('0') << (int)data[i];

	 return ss.str();
}

int cli_create( 	 
	string farmer_key,
	string pool_key,
	std::filesystem::path finaldir, 
	std::filesystem::path tempdir,
	std::filesystem::path tempdir2,
	wstring filename,
	string memo,
	string id,
	uint8_t k,
	uint32_t num_buckets,
	uint32_t num_stripes,
	uint8_t num_threads,
	uint32_t bufferSz,
	bool nobitfield
) {
try {
	cout << "creating plot..." << endl;
	std::random_device r;
	std::default_random_engine randomEngine(r());
	std::uniform_int_distribution<int> uniformDist(0, UCHAR_MAX);
	std::vector<uint8_t> sk_data(32);
	std::generate(sk_data.begin(), sk_data.end(), [&uniformDist, &randomEngine] () {
		return uniformDist(randomEngine);
	});

	PrivateKey sk = KeyGen(Bytes(sk_data));
	cout << "sk        = " << hexStr(sk.Serialize()) << endl;

	G1Element local_pk = master_sk_to_local_sk(sk).GetG1Element();
	cout << "local pk  = " << hexStr(local_pk.Serialize()) << endl;

	std::vector<uint8_t> farmer_key_data(48);
	HexToBytes(farmer_key,farmer_key_data.data());
	G1Element farmer_pk = G1Element::FromByteVector(farmer_key_data);
	cout << "farmer pk = " << hexStr(farmer_pk.Serialize()) << endl;

	G1Element plot_pk = local_pk + farmer_pk;
	cout << "plot pk   = " << hexStr(plot_pk.Serialize()) << endl;

	std::vector<uint8_t> pool_key_data(48);
	HexToBytes(pool_key,pool_key_data.data());
	G1Element pool_pk = G1Element::FromByteVector(pool_key_data);
	cout << "pool pk   = " << hexStr(pool_pk.Serialize()) << endl;

	std::vector<uint8_t> pool_key_bytes = pool_pk.Serialize();
	std::vector<uint8_t> plot_key_bytes = plot_pk.Serialize();
	std::vector<uint8_t> farm_key_bytes = farmer_pk.Serialize();
	std::vector<uint8_t> mstr_key_bytes = sk.Serialize();
			
	std::vector<uint8_t> plid_key_bytes;
	plid_key_bytes.insert(plid_key_bytes.end(), pool_key_bytes.begin(), pool_key_bytes.end());
	plid_key_bytes.insert(plid_key_bytes.end(), plot_key_bytes.begin(), plot_key_bytes.end());

	std::vector<uint8_t> plot_id(32);
	Hash256(plot_id.data(),plid_key_bytes.data(),plid_key_bytes.size());
	id = hexStr(plot_id);
	cout << "plot id   = " << id << endl;

	std::vector<uint8_t> memo_data;
	memo_data.insert(memo_data.end(), pool_key_bytes.begin(), pool_key_bytes.end());
	memo_data.insert(memo_data.end(), farm_key_bytes.begin(), farm_key_bytes.end());
	memo_data.insert(memo_data.end(), mstr_key_bytes.begin(), mstr_key_bytes.end());
	memo = hexStr(memo_data);
	cout << "memo      = " << memo << endl;

	time_t t = std::time(nullptr);
	std::tm tm = *std::localtime(&t);
	std::wostringstream oss;
	oss << std::put_time(&tm, L"%Y-%m-%d-%H-%M");
	wstring timestr = oss.str();

	filename = L"plot-k"+std::to_wstring((int)k)+L"-"+timestr+L"-"+s2ws(id)+L".plot";
	std::filesystem::path destPath = std::filesystem::path(finaldir);
	if (std::filesystem::exists(destPath)) {
		if (std::filesystem::is_regular_file(destPath)) {
			destPath = destPath.parent_path() / filename;
		}
		else {
			destPath = destPath / filename;
		}
	}
	else {
		if (std::filesystem::create_directory(destPath)) {
			destPath = destPath / filename;
		}
		else {
			std::cerr << "unable to create directory " << destPath.string() << std::endl;
			exit(1);
		}
	}
	if(std::filesystem::exists(destPath)) {
		std::cerr << "plot file already exists " << destPath.string() << std::endl;
		exit(1);
	}

	std::filesystem::path tempPath = std::filesystem::path(tempdir);
	if (std::filesystem::exists(tempPath)) {
		if (std::filesystem::is_regular_file(tempPath)) {
			tempPath = tempPath.parent_path();
		}
	}
	else {
		if (std::filesystem::create_directory(tempPath)) {
		}
		else {
			std::cerr << "unable to create temp directory " << tempPath.string() << std::endl;
			exit(1);
		}
	}
	tempdir = tempPath.string();

	if (tempdir2.empty()) {
		tempdir2 = tempdir;
	}

	std::filesystem::path tempPath2 = std::filesystem::path(tempdir2);
	if (std::filesystem::exists(tempPath2)) {
		if (std::filesystem::is_regular_file(tempPath2)) {
			tempPath2 = tempPath2.parent_path();
		}
	}
	else {
		if (std::filesystem::create_directory(tempPath2)) {
		}
		else {
			std::cerr << "unable to create temp2 directory " << tempPath2.string() << std::endl;
			exit(1);
		}
	}
	tempdir2 = tempPath2.string();

	wcout << L"Generating plot for k=" << static_cast<int>(k) << " filename=" << filename << endl << endl;

	id = Strip0x(id);
	if (id.size() != 64) {
		cout << "Invalid ID, should be 32 bytes (hex)" << endl;
		exit(1);
	}
	memo = Strip0x(memo);
	if (memo.size() % 2 != 0) {
		cout << "Invalid memo, should be only whole bytes (hex)" << endl;
		exit(1);
	}
	std::vector<uint8_t> memo_bytes(memo.size() / 2);
	std::array<uint8_t, 32> id_bytes;

	HexToBytes(memo, memo_bytes.data());
	HexToBytes(id, id_bytes.data());

	DiskPlotter plotter = DiskPlotter();
	uint8_t phases_flags = 0;
	if (!nobitfield) {
		phases_flags = ENABLE_BITFIELD;
	}
	phases_flags = phases_flags | SHOW_PROGRESS;
	plotter.CreatePlotDisk(
			tempdir,
			tempdir2,
			finaldir,
			filename,
			k,
			memo_bytes.data(),
			memo_bytes.size(),
			id_bytes.data(),
			id_bytes.size(),
			bufferSz,
			num_buckets,
			num_stripes,
			num_threads,
			phases_flags,
			true);
	}
	catch(const std::runtime_error& re)
	{
		std::cerr << "Runtime error: " << re.what() << std::endl;
		exit(1);
	}
	catch(const std::exception& ex)
	{
		std::cerr << "Error occurred: " << ex.what() << std::endl;
		exit(1);
	}
	catch(...)
	{
		std::cerr << "Unknown failure occurred. Possible memory corruption" << std::endl;
		exit(1);
	}
	return 1;
}

int cli_check(uint32_t iterations, std::wstring filename) {
	DiskProver prover(filename);
	Verifier verifier = Verifier();

	uint32_t success = 0;
	uint8_t id_bytes[32];
	prover.GetId(id_bytes);
	uint8_t k = prover.GetSize();

	for (uint32_t num = 0; num < iterations; num++) {
		vector<unsigned char> hash_input = intToBytes(num, 4);
		hash_input.insert(hash_input.end(), &id_bytes[0], &id_bytes[32]);

		vector<unsigned char> hash(picosha2::k_digest_size);
		picosha2::hash256(hash_input.begin(), hash_input.end(), hash.begin(), hash.end());

		try {
			vector<LargeBits> qualities = prover.GetQualitiesForChallenge(hash.data());

			for (uint32_t i = 0; i < qualities.size(); i++) {
				LargeBits proof = prover.GetFullProof(hash.data(), i);
				uint8_t *proof_data = new uint8_t[proof.GetSize() / 8];
				proof.ToBytes(proof_data);
				cout << "i: " << num << std::endl;
				cout << "challenge: 0x" << Util::HexStr(hash.data(), 256 / 8) << endl;
				cout << "proof: 0x" << Util::HexStr(proof_data, k * 8) << endl;
				LargeBits quality =
					verifier.ValidateProof(id_bytes, k, hash.data(), proof_data, k * 8);
				if (quality.GetSize() == 256 && quality == qualities[i]) {
					cout << "quality: " << quality << endl;
					cout << "Proof verification suceeded. k = " << static_cast<int>(k) << endl;
					success++;
				} else {
					cout << "Proof verification failed." << endl;
				}
				delete[] proof_data;
			}
		} catch (const std::exception& error) {
			cout << "Threw: " << error.what() << endl;
			continue;
		}
	}
	std::cout << "Total success: " << success << "/" << iterations << ", "
				<< (success * 100 / static_cast<double>(iterations)) << "%." << std::endl;
	//progress(4, 1, 1);
	return 0;
}

int cli_verify(std::string id, std::string proof, std::string challenge) {
	Verifier verifier = Verifier();

	id = Strip0x(id);
	proof = Strip0x(proof);
	challenge = Strip0x(challenge);
	if (id.size() != 64) {
		cout << "Invalid ID, should be 32 bytes" << endl;
		exit(1);
	}
	if (challenge.size() != 64) {
		cout << "Invalid challenge, should be 32 bytes" << endl;
		exit(1);
	}
	if (proof.size() % 16) {
		cout << "Invalid proof, should be a multiple of 8 bytes" << endl;
		exit(1);
	}
	uint8_t k = proof.size() / 16;
	cout << "Verifying proof=" << proof << " for challenge=" << challenge
			<< " and k=" << static_cast<int>(k) << endl
			<< endl;
	uint8_t id_bytes[32];
	uint8_t challenge_bytes[32];
	uint8_t *proof_bytes = new uint8_t[proof.size() / 2];
	HexToBytes(id, id_bytes);
	HexToBytes(challenge, challenge_bytes);
	HexToBytes(proof, proof_bytes);

	LargeBits quality =
		verifier.ValidateProof(id_bytes, k, challenge_bytes, proof_bytes, k * 8);
	if (quality.GetSize() == 256) {
		cout << "Proof verification succeeded. Quality: " << quality << endl;
	} else {
		cout << "Proof verification failed." << endl;
		exit(1);
	}
	delete[] proof_bytes;

	return 0;
}

int cli_proof(std::string challenge, std::wstring filename) {
	cout << "Proving using challenge=" << challenge << endl << endl;
	challenge = Strip0x(challenge);
	if (challenge.size() != 64) {
		cout << "Invalid challenge, should be 32 bytes" << endl;
		exit(1);
	}
	uint8_t challenge_bytes[32];
	HexToBytes(challenge, challenge_bytes);

	DiskProver prover(filename);
	try {
		vector<LargeBits> qualities = prover.GetQualitiesForChallenge(challenge_bytes);
		for (uint32_t i = 0; i < qualities.size(); i++) {
			uint8_t k = prover.GetSize();
			uint8_t *proof_data = new uint8_t[8 * k];
			LargeBits proof = prover.GetFullProof(challenge_bytes, i);
			proof.ToBytes(proof_data);
			cout << "Proof: 0x" << Util::HexStr(proof_data, k * 8) << endl;
			delete[] proof_data;
		}
		if (qualities.empty()) {
			cout << "No proofs found." << endl;
			exit(1);
		}
	} catch (const std::exception& ex) {
		std::cout << "Error proving. " << ex.what() << std::endl;
		exit(1);
	} catch (...) {
		std::cout << "Error proving. " << std::endl;
		exit(1);
	}
	return 1;
}
