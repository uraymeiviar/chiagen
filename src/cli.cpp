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
#include "madmax/phase1.hpp"
#include "madmax/phase2.hpp"
#include "madmax/phase3.hpp"
#include "madmax/phase4.hpp"
#include "madmax/chia.h"
#include "madmax/copy.h"
#include <ctime>
#include <iomanip>
#include <codecvt>
#include "Keygen.hpp"
#include "JobCreatePlotRef.h"
#include "JobCreatePlotMax.h"

using std::string;
using std::wstring;
using std::vector;
using std::endl;
using std::cout;
using std::wcout;

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
	JobCreatePlotRefParam param;
	param.bitfield = !nobitfield;
	param.buckets = num_buckets;
	param.buffer = bufferSz;
	param.destPath = finaldir.string();
	param.tempPath = tempdir.string();
	param.temp2Path = tempdir2.string();
	param.poolKey = pool_key;
	param.farmKey = farmer_key;
	param.id = id;
	param.filename = ws2s(filename);
	param.memo = memo;

	std::shared_ptr<JobCreatePlotRef> job = std::make_shared<JobCreatePlotRef>("cli","cli",param);
	job->start(true);
	if(job->activity) {
		job->activity->waitUntilFinish();
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
				cout << "challenge: 0x" << HexStr(hash.data(), 256 / 8) << endl;
				cout << "proof: 0x" << HexStr(proof_data, k * 8) << endl;
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
			uint8_t *proof_data = new uint8_t[8 * (size_t)k];
			LargeBits proof = prover.GetFullProof(challenge_bytes, i);
			proof.ToBytes(proof_data);
			cout << "Proof: 0x" << HexStr(proof_data, (size_t)k * 8) << endl;
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

int cli_create_mad(
	std::string farmer_key, 
	std::string pool_key, 
	std::filesystem::path finaldir, 
	std::filesystem::path tempdir, 
	std::filesystem::path tempdir2, 
	uint32_t num_buckets, 
	uint8_t num_threads)
{
	JobCreatePlotMaxParam param;
	param.destPath = finaldir.string();
	param.tempPath = tempdir.string();
	param.temp2Path = tempdir2.string();
	param.poolKey = pool_key;
	param.farmKey = farmer_key;
	param.threads = num_threads;
	param.buckets = num_buckets;

	std::shared_ptr<JobCreatePlotMax> job = std::make_shared<JobCreatePlotMax>("cli","cli",param);
	job->start(true);
	if(job->activity) {
		job->activity->waitUntilFinish();
	}
	return 1;
}
