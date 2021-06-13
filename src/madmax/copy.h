/*
 * copy.h
 *
 *  Created on: Jun 8, 2021
 *      Author: mad
 */

#ifndef INCLUDE_CHIA_COPY_H_
#define INCLUDE_CHIA_COPY_H_

#include "settings.h"

#include <string>
#include <vector>
#include <stdexcept>

#include <cstdio>
#include <cstdint>

namespace mad {
	inline
	uint64_t copy_file(const std::wstring& src_path, const std::wstring& dst_path)
	{
		FILE* src = FOPEN(src_path.c_str(), L"rb");
		if(!src) {
			throw std::runtime_error("fopen() failed");
		}
		FILE* dst = FOPEN(dst_path.c_str(), L"wb");
		if(!dst) {
			throw std::runtime_error("fopen() failed");
		}
		uint64_t total_bytes = 0;
		std::vector<uint8_t> buffer(g_read_chunk_size);
		while(true) {
			const auto num_bytes = fread(buffer.data(), 1, buffer.size(), src);
			if(fwrite(buffer.data(), 1, num_bytes, dst) != num_bytes) {
				throw std::runtime_error("fwrite() failed");
			}
			total_bytes += num_bytes;
			if(num_bytes < buffer.size()) {
				break;
			}
		}
		if(fclose(dst)) {
			throw std::runtime_error("fclose() failed");
		}
		fclose(src);
		return total_bytes;
	}

	inline
	uint64_t final_copy(const std::wstring& src_path, const std::wstring& dst_path)
	{
		if(src_path == dst_path) {
			return 0;
		}
		const std::wstring tmp_dst_path = dst_path + L".tmp";
		uint64_t total_bytes = 0;
		if(_wrename(src_path.c_str(), tmp_dst_path.c_str())) {
			// try manual copy
			total_bytes = copy_file(src_path, tmp_dst_path);
		}
		_wremove(src_path.c_str());
		_wrename(tmp_dst_path.c_str(), dst_path.c_str());
		return total_bytes;
	}
}


#endif /* INCLUDE_CHIA_COPY_H_ */
