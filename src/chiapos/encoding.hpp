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

#ifndef CHIAPOS_SRC_CPP_ENCODING_HPP_
#define CHIAPOS_SRC_CPP_ENCODING_HPP_

#include <cmath>
#include <map>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "fse/fse.h"
#include "fse/hist.h"
#include "fse/error_public.h"
#include "bits.hpp"
#include "util.hpp"

#include <mutex>

class TMemoCache {
public:
	~TMemoCache();

	bool CTExists(double R);
	bool DTExists(double R);   
	void CTAssign(double R, FSE_CTable *ct);
	void DTAssign(double R, FSE_DTable *dt);
	FSE_CTable *CTGet(double R);
	FSE_DTable *DTGet(double R);

private:
	mutable std::mutex memoMutex; // Mutex to ensure map thread safety
	std::map<double, FSE_CTable *> CT_MEMO;
	std::map<double, FSE_DTable *> DT_MEMO;
};

class Encoding {
public:
	// Calculates x * (x-1) / 2. Division is done before multiplication.
	static uint128_t GetXEnc(uint64_t x);

	// Encodes two max k bit values into one max 2k bit value. This can be thought of
	// mapping points in a two dimensional space into a one dimensional space. The benefits
	// of this are that we can store these line points efficiently, by sorting them, and only
	// storing the differences between them. Representing numbers as pairs in two
	// dimensions limits the compression strategies that can be used.
	// The x and y here represent table positions in previous tables.
	static uint128_t SquareToLinePoint(uint64_t x, uint64_t y);

	// Does the opposite as the above function, deterministicaly mapping a one dimensional
	// line point into a 2d pair. However, we do not recover the original ordering here.
	static std::pair<uint64_t, uint64_t> LinePointToSquare(uint128_t index);
	static std::vector<short> CreateNormalizedCount(double R);
	static size_t ANSEncodeDeltas(std::vector<unsigned char> deltas, double R, uint8_t *out);
	static void ANSFree(double R);

	static std::vector<uint8_t> ANSDecodeDeltas(
		TMemoCache& tmCache,
		const uint8_t *inp,
		size_t inp_size,
		int numDeltas,
		double R);
};

#endif  // SRC_CPP_ENCODING_HPP_
