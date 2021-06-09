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

#ifndef CHIAPOS_SRC_CPP_PHASES_HPP_
#define CHIAPOS_SRC_CPP_PHASES_HPP_

#ifndef _WIN32
#include <semaphore.h>
#include <unistd.h>
#endif

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <thread>
#include <memory>
#include <mutex>

#include "calculate_bucket.hpp"
#include "entry_sizes.hpp"
#include "pos_constants.hpp"
#include "sort_manager.hpp"
#include "thread_pool.hpp"
#include "util.hpp"
#include <cstdint>

#include "data.hpp"

enum phase_flags : uint8_t {
	ENABLE_BITFIELD = 1 << 0,
	SHOW_PROGRESS = 1 << 1,
};

PlotEntry GetLeftEntry(
	uint8_t const table_index,
	uint8_t const* const left_buf,
	uint8_t const k,
	uint8_t const metadata_size,
	uint8_t const pos_size);

void* phase1_thread(DiskPlotterContext* context,THREADDATA* ptd);

void* F1thread(
	DiskPlotterContext* context,
	int const index, 
	uint8_t const k, 
	const uint8_t* id, 
	std::mutex* smm
);

// This is Phase 1, or forward propagation. During this phase, all of the 7 tables,
// and f functions, are evaluated. The result is an intermediate plot file, that is
// several times larger than what the final file will be, but that has all of the
// proofs of space in it. First, F1 is computed, which is special since it uses
// ChaCha8, and each encryption provides multiple output values. Then, the rest of the
// f functions are computed, and a sort on disk happens for each table.
std::vector<uint64_t> RunPhase1(
	DiskPlotterContext& context,
	std::vector<FileDisk>& tmp_1_disks,
	uint8_t const k,
	const uint8_t* const id,
	std::wstring const tmp_dirname,
	std::wstring const filename,
	uint64_t const memory_size,
	uint32_t const num_buckets,
	uint32_t const log_num_buckets,
	uint32_t const stripe_size,
	uint8_t const num_threads,
	uint8_t const flags,
	bool show_progress
);


struct Phase2Results
{
	Disk& disk_for_table(int const table_index);
	FilteredDisk table1;
	BufferedDisk table7;
	std::vector<std::unique_ptr<SortManager>> output_files;
	std::vector<uint64_t> table_sizes;
};

// Backpropagate takes in as input, a file on which forward propagation has been done.
// The purpose of backpropagate is to eliminate any dead entries that don't contribute
// to final values in f7, to minimize disk usage. A sort on disk is applied to each table,
// so that they are sorted by position.
Phase2Results RunPhase2(
	DiskPlotterContext& context,
	std::vector<FileDisk> &tmp_1_disks,
	std::vector<uint64_t> table_sizes,
	uint8_t const k,
	const uint8_t *id,
	const std::wstring &tmp_dirname,
	const std::wstring &filename,
	uint64_t memory_size,
	uint32_t const num_buckets,
	uint32_t const log_num_buckets,
	uint8_t const flags);


// Results of phase 3. These are passed into Phase 4, so the checkpoint tables
// can be properly built.
struct Phase3Results {
	// Pointers to each table start byet in the final file
	std::vector<uint64_t> final_table_begin_pointers;
	// Number of entries written for f7
	uint64_t final_entries_written;
	uint32_t right_entry_size_bits;

	uint32_t header_size;
	std::unique_ptr<SortManager> table7_sm;
};

// This writes a number of entries into a file, in the final, optimized format. The park
// contains a checkpoint value (which is a 2k bits line point), as well as EPP (entries per
// park) entries. These entries are each divided into stub and delta section. The stub bits are
// encoded as is, but the delta bits are optimized into a variable encoding scheme. Since we
// have many entries in each park, we can approximate how much space each park with take. Format
// is: [2k bits of first_line_point]  [EPP-1 stubs] [Deltas size] [EPP-1 deltas]....
// [first_line_point] ...
void WriteParkToFile(
	Disk &final_disk,
	uint64_t table_start,
	uint64_t park_index,
	uint32_t park_size_bytes,
	uint128_t first_line_point,
	const std::vector<uint8_t> &park_deltas,
	const std::vector<uint64_t> &park_stubs,
	uint8_t k,
	uint8_t table_index,
	uint8_t *park_buffer,
	uint64_t const park_buffer_size
);

// Compresses the plot file tables into the final file. In order to do this, entries must be
// reorganized from the (pos, offset) bucket sorting order, to a more free line_point sorting
// order. In (pos, offset ordering), we store two pointers two the previous table, (x, y) which
// are very close together, by storing  (x, y-x), or (pos, offset), which can be done in about k
// + 8 bits, since y is in the next bucket as x. In order to decrease this, We store the actual
// entries from the previous table (e1, e2), instead of pos, offset pointers, and sort the
// entire table by (e1,e2). Then, the deltas between each (e1, e2) can be stored, which require
// around k bits.

// Converting into this format requires a few passes and sorts on disk. It also assumes that the
// backpropagation step happened, so there will be no more dropped entries. See the design
// document for more details on the algorithm.
Phase3Results RunPhase3(
	DiskPlotterContext& context,
	uint8_t k,
	FileDisk &tmp2_disk /*filename*/,
	Phase2Results res2,
	const uint8_t *id,
	const std::wstring &tmp_dirname,
	const std::wstring &filename,
	uint32_t header_size,
	uint64_t memory_size,
	uint32_t num_buckets,
	uint32_t log_num_buckets,
	const uint8_t flags
);


// Writes the checkpoint tables. The purpose of these tables, is to store a list of ~2^k values
// of size k (the proof of space outputs from table 7), in a way where they can be looked up for
// proofs, but also efficiently. To do this, we assume table 7 is sorted by f7, and we write the
// deltas between each f7 (which will be mostly 1s and 0s), with a variable encoding scheme
// (C3). Furthermore, we create C1 checkpoints along the way.  For example, every 10,000 f7
// entries, we can have a C1 checkpoint, and a C3 delta encoded entry with 10,000 deltas.

// Since we can't store all the checkpoints in
// memory for large plots, we create checkpoints for the checkpoints (C2), that are meant to be
// stored in memory during proving. For example, every 10,000 C1 entries, we can have a C2
// entry.

// The final table format for the checkpoints will be:
// C1 (checkpoint values)
// C2 (checkpoint values into)
// C3 (deltas of f7s between C1 checkpoints)
void RunPhase4(
	uint8_t k, 
	uint8_t pos_size, 
	FileDisk &tmp2_disk, 
	Phase3Results &res,
	const uint8_t flags, 
	const int max_phase4_progress_updates
);

#endif  // SRC_CPP_PHASES_HPP
