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

#ifndef CHIAPOS_SRC_CPP_B17PHASE2_HPP_
#define CHIAPOS_SRC_CPP_B17PHASE2_HPP_

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "bits.hpp"
#include "calculate_bucket.hpp"
#include "disk.hpp"
#include "util.hpp"

#include "disk.hpp"
#include "entry_sizes.hpp"
#include "b17.hpp"
#include "data.hpp"

class b17SortManager {
public:
	b17SortManager(
		DiskPlotterContext& context,
		uint8_t *memory,
		uint64_t memory_size,
		uint32_t num_buckets,
		uint32_t log_num_buckets,
		uint16_t entry_size,
		const std::wstring &tmp_dirname,
		const std::wstring &filename,
		uint32_t begin_bits,
		uint64_t stripe_size);

	void AddToCache(const Bits &entry);
	void AddToCache(const uint8_t *entry);

	uint8_t *ReadEntry(uint64_t position, int quicksort = 0);
	bool CloseToNewBucket(uint64_t position) const;
	void TriggerNewBucket(uint64_t position, bool quicksort);
	void ChangeMemory(uint8_t *new_memory, uint64_t new_memory_size);
	void FlushCache();

	~b17SortManager();
	DiskPlotterContext& context;
private:
	// Start of the whole memory array. This will be diveded into buckets
	uint8_t *memory_start;
	// Size of the whole memory array
	uint64_t memory_size;
	// One file for each bucket
	std::vector<FileDisk> bucket_files;
	// Size of each entry
	uint16_t entry_size;
	// Bucket determined by the first "log_num_buckets" bits starting at "begin_bits"
	uint32_t begin_bits;
	// Portion of memory to allocate to each bucket
	uint64_t size_per_bucket;
	// Log of the number of buckets; num bits to use to determine bucket
	uint32_t log_num_buckets;
	// One pointer to the start of each bucket memory
	std::vector<uint8_t *> mem_bucket_pointers;
	// The number of entries written to each bucket
	std::vector<uint64_t> mem_bucket_sizes;
	// The amount of data written to each disk bucket
	std::vector<uint64_t> bucket_write_pointers;
	uint64_t prev_bucket_buf_size;
	uint8_t *prev_bucket_buf;
	uint64_t prev_bucket_position_start;

	bool done;

	uint64_t final_position_start;
	uint64_t final_position_end;
	uint64_t next_bucket_to_sort;
	uint8_t *entry_buf;

	void FlushTable(uint16_t bucket_i);
	void SortBucket(int quicksort);
};

// Backpropagate takes in as input, a file on which forward propagation has been done.
// The purpose of backpropagate is to eliminate any dead entries that don't contribute
// to final values in f7, to minimize disk usage. A sort on disk is applied to each table,
// so that they are sorted by position.
std::vector<uint64_t> b17RunPhase2(
	DiskPlotterContext& context,
	uint8_t *memory,
	std::vector<FileDisk> &tmp_1_disks,
	std::vector<uint64_t> table_sizes,
	uint8_t k,
	const uint8_t *id,
	const std::wstring &tmp_dirname,
	const std::wstring &filename,
	uint64_t memory_size,
	uint32_t num_buckets,
	uint32_t log_num_buckets,
	const uint8_t flags
);

// Results of phase 3. These are passed into Phase 4, so the checkpoint tables
// can be properly built.
struct b17Phase3Results {
	// Pointers to each table start byet in the final file
	std::vector<uint64_t> final_table_begin_pointers;
	// Number of entries written for f7
	uint64_t final_entries_written;
	uint32_t right_entry_size_bits;
	uint32_t header_size;
	std::unique_ptr<b17SortManager> table7_sm;
};

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
b17Phase3Results b17RunPhase3(
	DiskPlotterContext& context,
	uint8_t *memory,
	uint8_t k,
	FileDisk &tmp2_disk /*filename*/,
	std::vector<FileDisk> &tmp_1_disks /*plot_filename*/,
	std::vector<uint64_t> table_sizes,
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
void b17RunPhase4(
	uint8_t k, 
	uint8_t pos_size, 
	FileDisk &tmp2_disk, 
	b17Phase3Results &res, 
	const uint8_t flags, 
	const int max_phase4_progress_updates
);
#endif