/*
 * settings.h
 *
 *  Created on: Jun 7, 2021
 *      Author: mad
 */

#ifndef INCLUDE_CHIA_SETTINGS_H_
#define INCLUDE_CHIA_SETTINGS_H_

#include <cstdint>
#include <cstddef>

namespace mad{
/*
 * Number of table entries to read at once.
 * default = 65536
 */
const size_t g_read_chunk_size = 65536;

/*
 * Number of table entries to buffer before writing to disk.
 * default = 4096
 */
const size_t g_write_chunk_size = 4096;
}

#endif /* INCLUDE_CHIA_SETTINGS_H_ */
