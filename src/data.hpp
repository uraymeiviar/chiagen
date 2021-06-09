#ifndef CHIAPOS_DATA_HPP
#define CHIAPOS_DATA_HPP

#include <memory>
#include <cstdint>
#include "thread_pool.hpp"

struct THREADDATA {
	int index;
	Sem::type* mine;
	Sem::type* theirs;
	uint64_t right_entry_size_bytes;
	uint8_t k;
	uint8_t table_index;
	uint8_t metadata_size;
	uint32_t entry_size_bytes;
	uint8_t pos_size;
	uint64_t prevtableentries;
	uint32_t compressed_entry_size_bytes;
	std::vector<FileDisk>* ptmp_1_disks;
};

class SortManager;

struct GlobalData {
	uint64_t left_writer_count;
	uint64_t right_writer_count;
	uint64_t matches;
	std::unique_ptr<SortManager> L_sort_manager;
	std::unique_ptr<SortManager> R_sort_manager;
	uint64_t left_writer_buf_entries;
	uint64_t left_writer;
	uint64_t right_writer;
	uint64_t stripe_size;
	uint8_t num_threads;
};

struct DiskPlotterContext {
	thread_pool pool;
	synced_stream sync_out;
	GlobalData globals;
};

#endif