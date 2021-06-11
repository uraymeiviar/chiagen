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

#ifndef CHIAPOS_SRC_CPP_PLOTTER_DISK_HPP_
#define  CHIAPOS_SRC_CPP_PLOTTER_DISK_HPP_

#ifndef _WIN32
#include <semaphore.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <memory>

#include "calculate_bucket.hpp"
#include "encoding.hpp"
#include "phases.hpp"
#include "b17.hpp"
#include "pos_constants.hpp"
#include "sort_manager.hpp"
#include "util.hpp"
#include "cli.hpp"
#include "data.hpp"

#define B17PHASE23

class DiskPlotter {
public:
	// This method creates a plot on disk with the filename. Many temporary files
	// (filename + ".table1.tmp", filename + ".p2.t3.sort_bucket_4.tmp", etc.) are created
	// and their total size will be larger than the final plot file. Temp files are deleted at the
	// end of the process.
	void CreatePlotDisk(
		std::wstring tmp_dirname,
		std::wstring tmp2_dirname,
		std::wstring final_dirname,
		std::wstring filename,
		uint8_t k,
		const uint8_t* memo,
		uint32_t memo_len,
		const uint8_t* id,
		uint32_t id_len,
		uint32_t buf_megabytes_input = 0,
		uint32_t num_buckets_input = 0,
		uint32_t stripe_size_input = 0,
		uint8_t num_threads_input = 0,
		bool nobitfield = false,
		bool show_progress = false);

	DiskPlotterContext context;
private:
	// Writes the plot file header to a file
	uint32_t WriteHeader(
		FileDisk& plot_Disk,
		uint8_t k,
		const uint8_t* id,
		const uint8_t* memo,
		uint32_t memo_len);
};

#endif  // SRC_CPP_PLOTTER_DISK_HPP_
