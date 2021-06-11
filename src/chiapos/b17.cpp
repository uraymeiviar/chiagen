#include "b17.hpp"
#include "encoding.hpp"
#include "phases.hpp"

std::vector<uint64_t> b17RunPhase2(
	DiskPlotterContext* context,
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
    const uint8_t flags)
{
    // An extra bit is used, since we may have more than 2^k entries in a table. (After pruning,
    // each table will have 0.8*2^k or less entries).
    uint8_t pos_size = k;

    std::vector<uint64_t> new_table_sizes = std::vector<uint64_t>(8, 0);
    new_table_sizes[7] = table_sizes[7];
    std::unique_ptr<b17SortManager> R_sort_manager;
    std::unique_ptr<b17SortManager> L_sort_manager;

    // Iterates through each table (with a left and right pointer), starting at 6 & 7.
    for (int table_index = 7; table_index > 1; --table_index) {
        // std::vector<std::pair<uint64_t, uint64_t> > match_positions;
        Timer table_timer;

        std::cout << "Backpropagating on table " << table_index << std::endl;

        uint16_t left_metadata_size = kVectorLens[table_index] * k;

        // The entry that we are reading (no metadata)
        uint16_t left_entry_size_bytes = EntrySizes::GetMaxEntrySize(k, table_index - 1, false);

        // The right entries which we read and write (the already have no metadata, since they
        // have been pruned in previous iteration)
        uint16_t right_entry_size_bytes = EntrySizes::GetMaxEntrySize(k, table_index, false);

        uint64_t left_reader = 0;
        uint64_t left_writer = 0;
        uint64_t right_reader = 0;
        uint64_t right_writer = 0;
        // The memory will be used like this, with most memory allocated towards the SortManager,
        // since it needs it
        // [--------------------------SM/RR-------------------------|-----------LW-------------|--RW--|--LR--]
        uint64_t sort_manager_buf_size = floor(kMemSortProportion * memory_size);
        uint64_t left_writer_buf_size = 3 * (memory_size - sort_manager_buf_size) / 4;
        uint64_t other_buf_sizes = (memory_size - sort_manager_buf_size - left_writer_buf_size) / 2;
        uint8_t *right_reader_buf = &(memory[0]);
        uint8_t *left_writer_buf = &(memory[sort_manager_buf_size]);
        uint8_t *right_writer_buf = &(memory[sort_manager_buf_size + left_writer_buf_size]);
        uint8_t *left_reader_buf =
            &(memory[sort_manager_buf_size + left_writer_buf_size + other_buf_sizes]);
        uint64_t right_reader_buf_entries = sort_manager_buf_size / right_entry_size_bytes;
        uint64_t left_writer_buf_entries = left_writer_buf_size / left_entry_size_bytes;
        uint64_t right_writer_buf_entries = other_buf_sizes / right_entry_size_bytes;
        uint64_t left_reader_buf_entries = other_buf_sizes / left_entry_size_bytes;
        uint64_t left_reader_count = 0;
        uint64_t right_reader_count = 0;
        uint64_t left_writer_count = 0;
        uint64_t right_writer_count = 0;

        if (table_index != 7) {
            R_sort_manager->ChangeMemory(memory, sort_manager_buf_size);
        }

        L_sort_manager = std::make_unique<b17SortManager>(
			context,
            left_writer_buf,
            left_writer_buf_size,
            num_buckets,
            log_num_buckets,
            left_entry_size_bytes,
            tmp_dirname,
            filename + L".p2.t" + std::to_wstring(table_index - 1),
            0,
            0);

        // We will divide by 2, so it must be even.
        assert(kCachedPositionsSize % 2 == 0);

        // Used positions will be used to mark which posL are present in table R, the rest will
        // be pruned
        bool used_positions[kCachedPositionsSize];
        memset(used_positions, 0, sizeof(used_positions));

        bool should_read_entry = true;

        // Cache for when we read a right entry that is too far forward
        uint64_t cached_entry_sort_key = 0;  // For table_index == 7, y is here
        uint64_t cached_entry_pos = 0;
        uint64_t cached_entry_offset = 0;

        uint64_t left_entry_counter = 0;  // Total left entries written

        // Sliding window map, from old position to new position (after pruning)
        uint64_t new_positions[kCachedPositionsSize];

        // Sort keys represent the ordering of entries, sorted by (y, pos, offset),
        // but using less bits (only k+1 instead of 2k + 9, etc.)
        // This is a map from old position to array of sort keys (one for each R entry with this
        // pos)
        uint64_t old_sort_keys[kReadMinusWrite][kMaxMatchesSingleEntry];
        // Map from old position to other positions that it matches with
        uint64_t old_offsets[kReadMinusWrite][kMaxMatchesSingleEntry];
        // Map from old position to count (number of times it appears)
        uint16_t old_counters[kReadMinusWrite];

        for (uint16_t &old_counter : old_counters) {
            old_counter = 0;
        }

        bool end_of_right_table = false;
        uint64_t current_pos = 0;  // This is the current pos that we are looking for in the L table
        uint64_t end_of_table_pos = 0;
        uint64_t greatest_pos = 0;  // This is the greatest position we have seen in R table

        // Buffers for reading and writing to disk
        uint8_t *left_entry_buf;
        uint8_t *new_left_entry_buf;
        uint8_t *right_entry_buf;
        uint8_t *right_entry_buf_SM = new uint8_t[right_entry_size_bytes];

        // Go through all right entries, and keep going since write pointer is behind read
        // pointer
        while (!end_of_right_table || (current_pos - end_of_table_pos <= kReadMinusWrite)) {
            old_counters[current_pos % kReadMinusWrite] = 0;

            // Resets used positions after a while, so we use little memory
            if ((current_pos - kReadMinusWrite) % (kCachedPositionsSize / 2) == 0) {
                if ((current_pos - kReadMinusWrite) % kCachedPositionsSize == 0) {
                    for (uint32_t i = kCachedPositionsSize / 2; i < kCachedPositionsSize; i++) {
                        used_positions[i] = false;
                    }
                } else {
                    for (uint32_t i = 0; i < kCachedPositionsSize / 2; i++) {
                        used_positions[i] = false;
                    }
                }
            }
            // Only runs this code if we are still reading the right table, or we still need to
            // read more left table entries (current_pos <= greatest_pos), otherwise, it skips
            // to the writing of the final R table entries
            if (!end_of_right_table || current_pos <= greatest_pos) {
                uint64_t entry_sort_key = 0;
                uint64_t entry_pos = 0;
                uint64_t entry_offset = 0;

                while (!end_of_right_table) {
                    if (should_read_entry) {
                        if (right_reader_count == new_table_sizes[table_index]) {
                            // Table R has ended, don't read any more (but keep writing)
                            end_of_right_table = true;
                            end_of_table_pos = current_pos;
                            break;
                        }
                        // Need to read another entry at the current position
                        if (table_index == 7) {
                            if (right_reader_count % right_reader_buf_entries == 0) {
                                uint64_t readAmt = std::min(
                                    right_reader_buf_entries * right_entry_size_bytes,
                                    (new_table_sizes[table_index] - right_reader_count) *
                                        right_entry_size_bytes);

                                tmp_1_disks[table_index].Read(
                                    right_reader, right_reader_buf, readAmt);
                                right_reader += readAmt;
                            }
                            right_entry_buf =
                                right_reader_buf + (right_reader_count % right_reader_buf_entries) *
                                                       right_entry_size_bytes;
                        } else {
                            right_entry_buf = R_sort_manager->ReadEntry(right_reader);
                            right_reader += right_entry_size_bytes;
                        }
                        right_reader_count++;

                        if (table_index == 7) {
                            // This is actually y for table 7
                            entry_sort_key = Util::SliceInt64FromBytes(right_entry_buf, 0, k);
                            entry_pos = Util::SliceInt64FromBytes(right_entry_buf, k, pos_size);
                            entry_offset = Util::SliceInt64FromBytes(
                                right_entry_buf, k + pos_size, kOffsetSize);
                        } else {
                            entry_pos = Util::SliceInt64FromBytes(right_entry_buf, 0, pos_size);
                            entry_offset =
                                Util::SliceInt64FromBytes(right_entry_buf, pos_size, kOffsetSize);
                            entry_sort_key = Util::SliceInt64FromBytes(
                                right_entry_buf, pos_size + kOffsetSize, k);
                        }
                    } else if (cached_entry_pos == current_pos) {
                        // We have a cached entry at this position
                        entry_sort_key = cached_entry_sort_key;
                        entry_pos = cached_entry_pos;
                        entry_offset = cached_entry_offset;
                    } else {
                        // The cached entry is at a later pos, so we don't read any more R
                        // entries, read more L entries instead.
                        break;
                    }

                    should_read_entry = true;  // By default, read another entry
                    if (entry_pos + entry_offset > greatest_pos) {
                        // Greatest L pos that we should look for
                        greatest_pos = entry_pos + entry_offset;
                    }

                    if (entry_pos == current_pos) {
                        // The current L position is the current R entry
                        // Marks the two matching entries as used (pos and pos+offset)
                        used_positions[entry_pos % kCachedPositionsSize] = true;
                        used_positions[(entry_pos + entry_offset) % kCachedPositionsSize] = true;

                        uint64_t old_write_pos = entry_pos % kReadMinusWrite;

                        // Stores the sort key for this R entry
                        old_sort_keys[old_write_pos][old_counters[old_write_pos]] = entry_sort_key;

                        // Stores the other matching pos for this R entry (pos6 + offset)
                        old_offsets[old_write_pos][old_counters[old_write_pos]] =
                            entry_pos + entry_offset;
                        ++old_counters[old_write_pos];
                    } else {
                        // Don't read any more right entries for now, because we haven't caught
                        // up on the left table yet
                        should_read_entry = false;
                        cached_entry_sort_key = entry_sort_key;
                        cached_entry_pos = entry_pos;
                        cached_entry_offset = entry_offset;
                        break;
                    }
                }
                // Only process left table if we still have entries - should fix read 0 issue
                if (left_reader_count < table_sizes[table_index - 1]) {
                    // ***Reads a left entry
                    if (left_reader_count % left_reader_buf_entries == 0) {
                        uint64_t readAmt = std::min(
                            left_reader_buf_entries * left_entry_size_bytes,
                            (table_sizes[table_index - 1] - left_reader_count) *
                                left_entry_size_bytes);
                        tmp_1_disks[table_index - 1].Read(left_reader, left_reader_buf, readAmt);
                        left_reader += readAmt;
                    }
                    left_entry_buf =
                        left_reader_buf +
                        (left_reader_count % left_reader_buf_entries) * left_entry_size_bytes;
                    left_reader_count++;

                    // If this left entry is used, we rewrite it. If it's not used, we ignore it.
                    if (used_positions[current_pos % kCachedPositionsSize]) {
                        uint64_t entry_metadata;

                        if (table_index > 2) {
                            // For tables 2-6, the entry is: pos, offset
                            entry_pos = Util::SliceInt64FromBytes(left_entry_buf, 0, pos_size);
                            entry_offset =
                                Util::SliceInt64FromBytes(left_entry_buf, pos_size, kOffsetSize);
                        } else {
                            entry_metadata =
                                Util::SliceInt64FromBytes(left_entry_buf, 0, left_metadata_size);
                        }

                        new_left_entry_buf =
                            left_writer_buf +
                            (left_writer_count % left_writer_buf_entries) * left_entry_size_bytes;
                        left_writer_count++;

                        Bits new_left_entry;
                        if (table_index > 2) {
                            // The new left entry is slightly different. Metadata is dropped, to
                            // save space, and the counter of the entry is written (sort_key). We
                            // use this instead of (y + pos + offset) since its smaller.
                            new_left_entry += Bits(entry_pos, pos_size);
                            new_left_entry += Bits(entry_offset, kOffsetSize);
                            new_left_entry += Bits(left_entry_counter, k);

                            // If we are not taking up all the bits, make sure they are zeroed
                            if (Util::ByteAlign(new_left_entry.GetSize()) <
                                left_entry_size_bytes * 8) {
                                new_left_entry +=
                                    Bits(0, left_entry_size_bytes * 8 - new_left_entry.GetSize());
                            }
                            L_sort_manager->AddToCache(new_left_entry);
                        } else {
                            // For table one entries, we don't care about sort key, only x.
                            // Also, we don't use the sort manager, since we won't sort it.
                            new_left_entry += Bits(entry_metadata, left_metadata_size);
                            new_left_entry.ToBytes(new_left_entry_buf);
                            if (left_writer_count % left_writer_buf_entries == 0) {
                                tmp_1_disks[table_index - 1].Write(
                                    left_writer,
                                    left_writer_buf,
                                    left_writer_buf_entries * left_entry_size_bytes);
                                left_writer += left_writer_buf_entries * left_entry_size_bytes;
                            }
                        }

                        // Mapped positions, so we can rewrite the R entry properly
                        new_positions[current_pos % kCachedPositionsSize] = left_entry_counter;

                        // Counter for new left entries written
                        ++left_entry_counter;
                    }
                }
            }
            // Write pointer lags behind the read pointer
            int64_t write_pointer_pos = current_pos - kReadMinusWrite + 1;

            // Only write entries for write_pointer_pos, if we are above 0, and there are
            // actually R entries for that pos.
            if (write_pointer_pos >= 0 &&
                used_positions[write_pointer_pos % kCachedPositionsSize]) {
                uint64_t new_pos = new_positions[write_pointer_pos % kCachedPositionsSize];
                Bits new_pos_bin(new_pos, pos_size);
                // There may be multiple R entries that share the write_pointer_pos, so write
                // all of them
                for (uint32_t counter = 0;
                     counter < old_counters[write_pointer_pos % kReadMinusWrite];
                     counter++) {
                    // Creates and writes the new right entry, with the cached data
                    uint64_t new_offset_pos = new_positions
                        [old_offsets[write_pointer_pos % kReadMinusWrite][counter] %
                         kCachedPositionsSize];
                    Bits new_right_entry =
                        table_index == 7
                            ? Bits(old_sort_keys[write_pointer_pos % kReadMinusWrite][counter], k)
                            : Bits(old_sort_keys[write_pointer_pos % kReadMinusWrite][counter], k);
                    new_right_entry += new_pos_bin;
                    // match_positions.push_back(std::make_pair(new_pos, new_offset_pos));
                    new_right_entry.AppendValue(new_offset_pos - new_pos, kOffsetSize);

                    // Calculate right entry pointer for output
                    right_entry_buf =
                        right_writer_buf +
                        (right_writer_count % right_writer_buf_entries) * right_entry_size_bytes;
                    right_writer_count++;

                    if (Util::ByteAlign(new_right_entry.GetSize()) < right_entry_size_bytes * 8) {
                        memset(right_entry_buf, 0, right_entry_size_bytes);
                    }
                    new_right_entry.ToBytes(right_entry_buf);
                    // Check for write out to disk
                    if (right_writer_count % right_writer_buf_entries == 0) {
                        tmp_1_disks[table_index].Write(
                            right_writer,
                            right_writer_buf,
                            right_writer_buf_entries * right_entry_size_bytes);
                        right_writer += right_writer_buf_entries * right_entry_size_bytes;
                    }
                }
            }
            ++current_pos;
        }
        new_table_sizes[table_index - 1] = left_entry_counter;

        std::cout << "\tWrote left entries: " << left_entry_counter << std::endl;
        table_timer.PrintElapsed("Total backpropagation time::");

        tmp_1_disks[table_index].Write(
            right_writer,
            right_writer_buf,
            (right_writer_count % right_writer_buf_entries) * right_entry_size_bytes);
        right_writer += (right_writer_count % right_writer_buf_entries) * right_entry_size_bytes;

        if (table_index != 7) {
            R_sort_manager.reset();
        }

        // Truncates the right table
        tmp_1_disks[table_index].Truncate(right_writer);

        if (table_index == 2) {
            // Writes remaining entries for table1
            tmp_1_disks[table_index - 1].Write(
                left_writer,
                left_writer_buf,
                (left_writer_count % left_writer_buf_entries) * left_entry_size_bytes);
            left_writer += (left_writer_count % left_writer_buf_entries) * left_entry_size_bytes;

            // Truncates the left table
            tmp_1_disks[table_index - 1].Truncate(left_writer);
        } else {
            L_sort_manager->FlushCache();
            R_sort_manager = std::move(L_sort_manager);
        }
        delete[] right_entry_buf_SM;
        //if (flags & SHOW_PROGRESS) {
        //    progress(2, 8 - table_index, 6);
        //}
    }
    L_sort_manager.reset();
    return new_table_sizes;
}

b17Phase3Results b17RunPhase3(
	DiskPlotterContext* context,
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
    const uint8_t flags)
{
    uint8_t pos_size = k;
    uint8_t line_point_size = 2 * k - 1;

    // file size is not really know at this point, but it doesn't matter as
    // we're only writing
    BufferedDisk tmp2_buffered_disk(&tmp2_disk, 0);

    std::vector<uint64_t> final_table_begin_pointers(12, 0);
    final_table_begin_pointers[1] = header_size;

    uint8_t table_pointer_bytes[8];
    Util::IntToEightBytes(table_pointer_bytes, final_table_begin_pointers[1]);
    tmp2_buffered_disk.Write(header_size - 10 * 8, table_pointer_bytes, 8);

    uint64_t final_entries_written = 0;
    uint32_t right_entry_size_bytes = 0;

    std::unique_ptr<b17SortManager> L_sort_manager;
    std::unique_ptr<b17SortManager> R_sort_manager;

    // These variables are used in the WriteParkToFile method. They are preallocatted here
    // to save time.
    uint64_t park_buffer_size = EntrySizes::CalculateLinePointSize(k) +
                                EntrySizes::CalculateStubsSize(k) + 2 +
                                EntrySizes::CalculateMaxDeltasSize(k, 1);
    uint8_t *park_buffer = new uint8_t[park_buffer_size];

    // Iterates through all tables, starting at 1, with L and R pointers.
    // For each table, R entries are rewritten with line points. Then, the right table is
    // sorted by line_point. After this, the right table entries are rewritten as (sort_key,
    // new_pos), where new_pos is the position in the table, where it's sorted by line_point,
    // and the line_points are written to disk to a final table. Finally, table_i is sorted by
    // sort_key. This allows us to compare to the next table.
    for (int table_index = 1; table_index < 7; table_index++) {
        Timer table_timer;
        Timer computation_pass_1_timer;
        std::cout << "Compressing tables " << table_index << " and " << (table_index + 1)
                  << std::endl;

        // The park size must be constant, for simplicity, but must be big enough to store EPP
        // entries. entry deltas are encoded with variable length, and thus there is no
        // guarantee that they won't override into the next park. It is only different (larger)
        // for table 1
        uint32_t park_size_bytes = EntrySizes::CalculateParkSize(k, table_index);

        // Sort key for table 7 is just y, which is k bits. For all other tables it can
        // be higher than 2^k and therefore k+1 bits are used.
        uint32_t right_sort_key_size = k;

        uint32_t left_entry_size_bytes = EntrySizes::GetMaxEntrySize(k, table_index, false);
        right_entry_size_bytes = EntrySizes::GetMaxEntrySize(k, table_index + 1, false);

        uint64_t left_reader = 0;
        uint64_t right_reader = 0;
        // The memory will be used like this, with most memory allocated towards the SortManager,
        // since it needs it
        // [---------------------------SM/LR---------------------|----------RW--------|---RR---]
        uint64_t sort_manager_buf_size = floor(kMemSortProportion * memory_size);
        uint64_t right_writer_buf_size = 3 * (memory_size - sort_manager_buf_size) / 4;
        uint64_t right_reader_buf_size =
            memory_size - sort_manager_buf_size - right_writer_buf_size;
        uint8_t *left_reader_buf = &(memory[0]);
        uint8_t *right_writer_buf = &(memory[sort_manager_buf_size]);
        uint8_t *right_reader_buf = &(memory[sort_manager_buf_size + right_writer_buf_size]);
        uint64_t left_reader_buf_entries = sort_manager_buf_size / left_entry_size_bytes;
        uint64_t right_reader_buf_entries = right_reader_buf_size / right_entry_size_bytes;
        uint64_t left_reader_count = 0;
        uint64_t right_reader_count = 0;
        uint64_t total_r_entries = 0;

        if (table_index > 1) {
            L_sort_manager->ChangeMemory(memory, sort_manager_buf_size);
        }

        R_sort_manager = std::make_unique<b17SortManager>(
			context,
            right_writer_buf,
            right_writer_buf_size,
            num_buckets,
            log_num_buckets,
            right_entry_size_bytes,
            tmp_dirname,
            filename + L".p3.t" + std::to_wstring(table_index + 1),
            0,
            0);

        bool should_read_entry = true;
        std::vector<uint64_t> left_new_pos(kCachedPositionsSize);

        uint64_t old_sort_keys[kReadMinusWrite][kMaxMatchesSingleEntry];
        uint64_t old_offsets[kReadMinusWrite][kMaxMatchesSingleEntry];
        uint16_t old_counters[kReadMinusWrite];
        for (uint16_t &old_counter : old_counters) {
            old_counter = 0;
        }
        bool end_of_right_table = false;
        uint64_t current_pos = 0;
        uint64_t end_of_table_pos = 0;
        uint64_t greatest_pos = 0;

        uint8_t *right_entry_buf;
        uint8_t *left_entry_disk_buf = left_reader_buf;
        uint8_t *left_entry_buf_sm = new uint8_t[left_entry_size_bytes];

        uint64_t entry_sort_key, entry_pos, entry_offset;
        uint64_t cached_entry_sort_key = 0;
        uint64_t cached_entry_pos = 0;
        uint64_t cached_entry_offset = 0;

        // Similar algorithm as Backprop, to read both L and R tables simultaneously
        while (!end_of_right_table || (current_pos - end_of_table_pos <= kReadMinusWrite)) {
            old_counters[current_pos % kReadMinusWrite] = 0;

            if (end_of_right_table || current_pos <= greatest_pos) {
                while (!end_of_right_table) {
                    if (should_read_entry) {
                        if (right_reader_count == table_sizes[table_index + 1]) {
                            end_of_right_table = true;
                            end_of_table_pos = current_pos;
                            break;
                        }
                        // The right entries are in the format from backprop, (sort_key, pos,
                        // offset)
                        if (right_reader_count % right_reader_buf_entries == 0) {
                            uint64_t readAmt = std::min(
                                right_reader_buf_entries * right_entry_size_bytes,
                                (table_sizes[table_index + 1] - right_reader_count) *
                                    right_entry_size_bytes);

                            tmp_1_disks[table_index + 1].Read(
                                right_reader, right_reader_buf, readAmt);
                            right_reader += readAmt;
                        }
                        right_entry_buf =
                            right_reader_buf + (right_reader_count % right_reader_buf_entries) *
                                                   right_entry_size_bytes;
                        right_reader_count++;

                        entry_sort_key =
                            Util::SliceInt64FromBytes(right_entry_buf, 0, right_sort_key_size);
                        entry_pos = Util::SliceInt64FromBytes(
                            right_entry_buf, right_sort_key_size, pos_size);
                        entry_offset = Util::SliceInt64FromBytes(
                            right_entry_buf, right_sort_key_size + pos_size, kOffsetSize);
                    } else if (cached_entry_pos == current_pos) {
                        entry_sort_key = cached_entry_sort_key;
                        entry_pos = cached_entry_pos;
                        entry_offset = cached_entry_offset;
                    } else {
                        break;
                    }

                    should_read_entry = true;

                    if (entry_pos + entry_offset > greatest_pos) {
                        greatest_pos = entry_pos + entry_offset;
                    }
                    if (entry_pos == current_pos) {
                        uint64_t old_write_pos = entry_pos % kReadMinusWrite;
                        old_sort_keys[old_write_pos][old_counters[old_write_pos]] = entry_sort_key;
                        old_offsets[old_write_pos][old_counters[old_write_pos]] =
                            (entry_pos + entry_offset);
                        ++old_counters[old_write_pos];
                    } else {
                        should_read_entry = false;
                        cached_entry_sort_key = entry_sort_key;
                        cached_entry_pos = entry_pos;
                        cached_entry_offset = entry_offset;
                        break;
                    }
                }
                if (left_reader_count < table_sizes[table_index]) {
                    // The left entries are in the new format: (sort_key, new_pos), except for table
                    // 1: (y, x).
                    if (table_index == 1) {
                        if (left_reader_count % left_reader_buf_entries == 0) {
                            uint64_t readAmt = std::min(
                                left_reader_buf_entries * left_entry_size_bytes,
                                (table_sizes[table_index] - left_reader_count) *
                                    left_entry_size_bytes);

                            tmp_1_disks[table_index].Read(left_reader, left_reader_buf, readAmt);
                            left_reader += readAmt;
                        }
                        left_entry_disk_buf =
                            left_reader_buf +
                            (left_reader_count % left_reader_buf_entries) * left_entry_size_bytes;
                    } else {
                        left_entry_disk_buf = L_sort_manager->ReadEntry(left_reader, 1);
                        left_reader += left_entry_size_bytes;
                    }
                    left_reader_count++;
                }

                // We read the "new_pos" from the L table, which for table 1 is just x. For
                // other tables, the new_pos
                if (table_index == 1) {
                    // Only k bits, since this is x
                    left_new_pos[current_pos % kCachedPositionsSize] =
                        Util::SliceInt64FromBytes(left_entry_disk_buf, 0, k);
                } else {
                    // k+1 bits in case it overflows
                    left_new_pos[current_pos % kCachedPositionsSize] =
                        Util::SliceInt64FromBytes(left_entry_disk_buf, right_sort_key_size, k);
                }
            }

            uint64_t write_pointer_pos = current_pos - kReadMinusWrite + 1;

            // Rewrites each right entry as (line_point, sort_key)
            if (current_pos + 1 >= kReadMinusWrite) {
                uint64_t left_new_pos_1 = left_new_pos[write_pointer_pos % kCachedPositionsSize];
                for (uint32_t counter = 0;
                     counter < old_counters[write_pointer_pos % kReadMinusWrite];
                     counter++) {
                    uint64_t left_new_pos_2 = left_new_pos
                        [old_offsets[write_pointer_pos % kReadMinusWrite][counter] %
                         kCachedPositionsSize];

                    // A line point is an encoding of two k bit values into one 2k bit value.
                    uint128_t line_point =
                        Encoding::SquareToLinePoint(left_new_pos_1, left_new_pos_2);

                    if (left_new_pos_1 > ((uint64_t)1 << k) ||
                        left_new_pos_2 > ((uint64_t)1 << k)) {
                        std::cout << "left or right positions too large" << std::endl;
                        std::cout << (line_point > ((uint128_t)1 << (2 * k)));
                        if ((line_point > ((uint128_t)1 << (2 * k)))) {
                            std::cout << "L, R: " << left_new_pos_1 << " " << left_new_pos_2
                                      << std::endl;
                            std::cout << "Line point: " << line_point << std::endl;
                            abort();
                        }
                    }
                    Bits to_write = Bits(line_point, line_point_size);
                    to_write += Bits(
                        old_sort_keys[write_pointer_pos % kReadMinusWrite][counter],
                        right_sort_key_size);

                    R_sort_manager->AddToCache(to_write);
                    total_r_entries++;
                }
            }
            current_pos += 1;
        }
        computation_pass_1_timer.PrintElapsed("\tFirst computation pass time:");

        // Remove no longer needed file
        tmp_1_disks[table_index].Truncate(0);

        // Flush cache so all entries are written to buckets
        R_sort_manager->FlushCache();

        delete[] left_entry_buf_sm;

        Timer computation_pass_2_timer;

        // The memory will be used like this, with most memory allocated towards the
        // LeftSortManager, since it needs it
        // [---------------------------LSM/RR-----------------------------------|---------RSM/RW---------]
        right_reader = 0;
        right_reader_buf_size = floor(kMemSortProportionLinePoint * memory_size);
        right_writer_buf_size = memory_size - right_reader_buf_size;
        right_reader_buf = &(memory[0]);
        right_writer_buf = &(memory[right_reader_buf_size]);
        right_reader_count = 0;
        uint64_t final_table_writer = final_table_begin_pointers[table_index];

        final_entries_written = 0;

        if (table_index > 1) {
            // Make sure all files are removed
            L_sort_manager.reset();
        }

        // L sort manager will be used for the writer, and R sort manager will be used for the
        // reader
        R_sort_manager->ChangeMemory(right_reader_buf, right_reader_buf_size);
        L_sort_manager = std::make_unique<b17SortManager>(
			context,
            right_writer_buf,
            right_writer_buf_size,
            num_buckets,
            log_num_buckets,
            right_entry_size_bytes,
            tmp_dirname,
            filename + L".p3s.t" + std::to_wstring(table_index + 1),
            0,
            0);

        std::vector<uint8_t> park_deltas;
        std::vector<uint64_t> park_stubs;
        uint128_t checkpoint_line_point = 0;
        uint128_t last_line_point = 0;
        uint64_t park_index = 0;

        uint8_t *right_reader_entry_buf;

        // Now we will write on of the final tables, since we have a table sorted by line point.
        // The final table will simply store the deltas between each line_point, in fixed space
        // groups(parks), with a checkpoint in each group.
        Bits right_entry_bits;
        int added_to_cache = 0;
        uint8_t index_size = table_index == 6 ? k + 1 : k;
        for (uint64_t index = 0; index < total_r_entries; index++) {
            right_reader_entry_buf = R_sort_manager->ReadEntry(right_reader, 2);
            right_reader += right_entry_size_bytes;
            right_reader_count++;

            // Right entry is read as (line_point, sort_key)
            uint128_t line_point =
                Util::SliceInt128FromBytes(right_reader_entry_buf, 0, line_point_size);
            uint64_t sort_key = Util::SliceInt64FromBytes(
                right_reader_entry_buf, line_point_size, right_sort_key_size);

            // Write the new position (index) and the sort key
            Bits to_write = Bits(sort_key, right_sort_key_size);
            to_write += Bits(index, index_size);

            L_sort_manager->AddToCache(to_write);
            added_to_cache++;

            // Every EPP entries, writes a park
            if (index % kEntriesPerPark == 0) {
                if (index != 0) {
                    WriteParkToFile(
                        tmp2_buffered_disk,
                        final_table_begin_pointers[table_index],
                        park_index,
                        park_size_bytes,
                        checkpoint_line_point,
                        park_deltas,
                        park_stubs,
                        k,
                        table_index,
                        park_buffer,
                        park_buffer_size);
                    park_index += 1;
                    final_entries_written += (park_stubs.size() + 1);
                }
                park_deltas.clear();
                park_stubs.clear();

                checkpoint_line_point = line_point;
            }
            uint128_t big_delta = line_point - last_line_point;

            // Since we have approx 2^k line_points between 0 and 2^2k, the average
            // space between them when sorted, is k bits. Much more efficient than storing each
            // line point. This is diveded into the stub and delta. The stub is the least
            // significant (k-kMinusStubs) bits, and largely random/incompressible. The small
            // delta is the rest, which can be efficiently encoded since it's usually very
            // small.

            uint64_t stub = big_delta & ((1ULL << (k - kStubMinusBits)) - 1);
            uint64_t small_delta = big_delta >> (k - kStubMinusBits);

            assert(small_delta < 256);

            if ((index % kEntriesPerPark != 0)) {
                park_deltas.push_back(small_delta);
                park_stubs.push_back(stub);
            }
            last_line_point = line_point;
        }
        R_sort_manager.reset();
        L_sort_manager->FlushCache();

        computation_pass_2_timer.PrintElapsed("\tSecond computation pass time:");

        if (park_deltas.size() > 0) {
            // Since we don't have a perfect multiple of EPP entries, this writes the last ones
            WriteParkToFile(
                tmp2_buffered_disk,
                final_table_begin_pointers[table_index],
                park_index,
                park_size_bytes,
                checkpoint_line_point,
                park_deltas,
                park_stubs,
                k,
                table_index,
                park_buffer,
                park_buffer_size);
            final_entries_written += (park_stubs.size() + 1);
        }

        Encoding::ANSFree(kRValues[table_index - 1]);
        std::cout << "\tWrote " << final_entries_written << " entries" << std::endl;

        final_table_begin_pointers[table_index + 1] =
            final_table_begin_pointers[table_index] + (park_index + 1) * park_size_bytes;

        final_table_writer = header_size - 8 * (10 - table_index);
        Util::IntToEightBytes(table_pointer_bytes, final_table_begin_pointers[table_index + 1]);
        tmp2_buffered_disk.Write(final_table_writer, (table_pointer_bytes), 8);
        final_table_writer += 8;

        table_timer.PrintElapsed("Total compress table time:");
        //if (flags & SHOW_PROGRESS) {
        //    progress(3, table_index, 6);
        //}
    }

    L_sort_manager->ChangeMemory(memory, memory_size);
    delete[] park_buffer;
    tmp2_buffered_disk.FreeMemory();

    // These results will be used to write table P7 and the checkpoint tables in phase 4.
    return b17Phase3Results{
        final_table_begin_pointers,
        final_entries_written,
        right_entry_size_bytes * 8,
        header_size,
        std::move(L_sort_manager)};
}

void b17RunPhase4(
    uint8_t k,
    uint8_t pos_size,
    FileDisk &tmp2_disk,
    b17Phase3Results &res,
    const uint8_t flags,
    const int max_phase4_progress_updates)
{
    uint32_t P7_park_size = Util::ByteAlign((k + 1) * kEntriesPerPark) / 8;
    uint64_t number_of_p7_parks =
        ((res.final_entries_written == 0 ? 0 : res.final_entries_written - 1) / kEntriesPerPark) +
        1;

    uint64_t begin_byte_C1 = res.final_table_begin_pointers[7] + number_of_p7_parks * P7_park_size;

    uint64_t total_C1_entries = cdiv(res.final_entries_written, kCheckpoint1Interval);
    uint64_t begin_byte_C2 = begin_byte_C1 + (total_C1_entries + 1) * (Util::ByteAlign(k) / 8);
    uint64_t total_C2_entries = cdiv(total_C1_entries, kCheckpoint2Interval);
    uint64_t begin_byte_C3 = begin_byte_C2 + (total_C2_entries + 1) * (Util::ByteAlign(k) / 8);

    uint32_t size_C3 = EntrySizes::CalculateC3Size(k);
    uint64_t end_byte = begin_byte_C3 + (total_C1_entries)*size_C3;

    res.final_table_begin_pointers[8] = begin_byte_C1;
    res.final_table_begin_pointers[9] = begin_byte_C2;
    res.final_table_begin_pointers[10] = begin_byte_C3;
    res.final_table_begin_pointers[11] = end_byte;

    uint64_t plot_file_reader = 0;
    uint64_t final_file_writer_1 = begin_byte_C1;
    uint64_t final_file_writer_2 = begin_byte_C3;
    uint64_t final_file_writer_3 = res.final_table_begin_pointers[7];

    uint64_t prev_y = 0;
    std::vector<Bits> C2;
    uint64_t num_C1_entries = 0;
    std::vector<uint8_t> deltas_to_write;
    uint32_t right_entry_size_bytes = res.right_entry_size_bits / 8;

    uint8_t *right_entry_buf;
    auto C1_entry_buf = new uint8_t[Util::ByteAlign(k) / 8];
    auto C3_entry_buf = new uint8_t[size_C3];
    auto P7_entry_buf = new uint8_t[P7_park_size];

    std::cout << "\tStarting to write C1 and C3 tables" << std::endl;

    ParkBits to_write_p7;
    const int progress_update_increment = res.final_entries_written / max_phase4_progress_updates;

    // We read each table7 entry, which is sorted by f7, but we don't need f7 anymore. Instead,
    // we will just store pos6, and the deltas in table C3, and checkpoints in tables C1 and C2.
    for (uint64_t f7_position = 0; f7_position < res.final_entries_written; f7_position++) {
        right_entry_buf = res.table7_sm->ReadEntry(plot_file_reader, 1);

        plot_file_reader += right_entry_size_bytes;
        uint64_t entry_y = Util::SliceInt64FromBytes(right_entry_buf, 0, k);
        uint64_t entry_new_pos = Util::SliceInt64FromBytes(right_entry_buf, k, pos_size);

        Bits entry_y_bits = Bits(entry_y, k);

        if (f7_position % kEntriesPerPark == 0 && f7_position > 0) {
            memset(P7_entry_buf, 0, P7_park_size);
            to_write_p7.ToBytes(P7_entry_buf);
            tmp2_disk.Write(final_file_writer_3, (P7_entry_buf), P7_park_size);
            final_file_writer_3 += P7_park_size;
            to_write_p7 = ParkBits();
        }

        to_write_p7 += ParkBits(entry_new_pos, k + 1);

        if (f7_position % kCheckpoint1Interval == 0) {
            entry_y_bits.ToBytes(C1_entry_buf);
            tmp2_disk.Write(final_file_writer_1, (C1_entry_buf), Util::ByteAlign(k) / 8);
            final_file_writer_1 += Util::ByteAlign(k) / 8;
            if (num_C1_entries > 0) {
                final_file_writer_2 = begin_byte_C3 + (num_C1_entries - 1) * size_C3;
                size_t num_bytes =
                    Encoding::ANSEncodeDeltas(deltas_to_write, kC3R, C3_entry_buf + 2) + 2;

                // We need to be careful because deltas are variable sized, and they need to fit
                assert(size_C3 * 8 > num_bytes);

                // Write the size
                Util::IntToTwoBytes(C3_entry_buf, num_bytes - 2);

                tmp2_disk.Write(final_file_writer_2, (C3_entry_buf), num_bytes);
                final_file_writer_2 += num_bytes;
            }
            prev_y = entry_y;
            if (f7_position % (kCheckpoint1Interval * kCheckpoint2Interval) == 0) {
                C2.emplace_back(std::move(entry_y_bits));
            }
            deltas_to_write.clear();
            ++num_C1_entries;
        } else {
            if (entry_y == prev_y) {
                deltas_to_write.push_back(0);
            } else {
                deltas_to_write.push_back(entry_y - prev_y);
            }
            prev_y = entry_y;
        }
        //if (flags & SHOW_PROGRESS && f7_position % progress_update_increment == 0) {
        //    progress(4, f7_position, res.final_entries_written);
        //}
    }
    Encoding::ANSFree(kC3R);
    res.table7_sm.reset();

    // Writes the final park to disk
    memset(P7_entry_buf, 0, P7_park_size);
    to_write_p7.ToBytes(P7_entry_buf);

    tmp2_disk.Write(final_file_writer_3, (P7_entry_buf), P7_park_size);
    final_file_writer_3 += P7_park_size;

    if (!deltas_to_write.empty()) {
        size_t num_bytes = Encoding::ANSEncodeDeltas(deltas_to_write, kC3R, C3_entry_buf + 2);
        memset(C3_entry_buf + num_bytes + 2, 0, size_C3 - (num_bytes + 2));
        final_file_writer_2 = begin_byte_C3 + (num_C1_entries - 1) * size_C3;

        // Write the size
        Util::IntToTwoBytes(C3_entry_buf, num_bytes);

        tmp2_disk.Write(final_file_writer_2, (C3_entry_buf), size_C3);
        final_file_writer_2 += size_C3;
        Encoding::ANSFree(kC3R);
    }

    Bits(0, Util::ByteAlign(k)).ToBytes(C1_entry_buf);
    tmp2_disk.Write(final_file_writer_1, (C1_entry_buf), Util::ByteAlign(k) / 8);
    final_file_writer_1 += Util::ByteAlign(k) / 8;
    std::cout << "\tFinished writing C1 and C3 tables" << std::endl;
    std::cout << "\tWriting C2 table" << std::endl;

    for (Bits &C2_entry : C2) {
        C2_entry.ToBytes(C1_entry_buf);
        tmp2_disk.Write(final_file_writer_1, (C1_entry_buf), Util::ByteAlign(k) / 8);
        final_file_writer_1 += Util::ByteAlign(k) / 8;
    }
    Bits(0, Util::ByteAlign(k)).ToBytes(C1_entry_buf);
    tmp2_disk.Write(final_file_writer_1, (C1_entry_buf), Util::ByteAlign(k) / 8);
    final_file_writer_1 += Util::ByteAlign(k) / 8;
    std::cout << "\tFinished writing C2 table" << std::endl;

    delete[] C3_entry_buf;
    delete[] C1_entry_buf;
    delete[] P7_entry_buf;

    final_file_writer_1 = res.header_size - 8 * 3;
    uint8_t table_pointer_bytes[8];

    // Writes the pointers to the start of the tables, for proving
    for (int i = 8; i <= 10; i++) {
        Util::IntToEightBytes(table_pointer_bytes, res.final_table_begin_pointers[i]);
        tmp2_disk.Write(final_file_writer_1, table_pointer_bytes, 8);
        final_file_writer_1 += 8;
    }

    std::cout << "\tFinal table pointers:" << std::endl << std::hex;

    for (int i = 1; i <= 10; i++) {
        std::cout << "\t" << (i < 8 ? "P" : "C") << (i < 8 ? i : i - 7);
        std::cout << ": 0x" << res.final_table_begin_pointers[i] << std::endl;
    }
    std::cout << std::dec;
}

b17SortManager::b17SortManager(
	DiskPlotterContext* context,
    uint8_t *memory,
    uint64_t memory_size,
    uint32_t num_buckets,
    uint32_t log_num_buckets,
    uint16_t entry_size,
    const std::wstring &tmp_dirname,
    const std::wstring &filename,
    uint32_t begin_bits,
    uint64_t stripe_size) : context(context)
{
    this->memory_start = memory;
    this->memory_size = memory_size;
    this->size_per_bucket = memory_size / num_buckets;
    this->log_num_buckets = log_num_buckets;
    this->entry_size = entry_size;
    this->begin_bits = begin_bits;
    this->done = false;
    this->prev_bucket_buf_size = 2 * (stripe_size + 10 * (kBC / pow(2, kExtraBits))) * entry_size;
    this->prev_bucket_buf = new uint8_t[this->prev_bucket_buf_size]();
    this->prev_bucket_position_start = 0;
    // Cross platform way to concatenate paths, gulrak library.
    std::vector<fs::path> bucket_filenames = std::vector<fs::path>();

    for (size_t bucket_i = 0; bucket_i < num_buckets; bucket_i++) {
        this->mem_bucket_pointers.push_back(memory + bucket_i * size_per_bucket);
        this->mem_bucket_sizes.push_back(0);
        this->bucket_write_pointers.push_back(0);
        std::ostringstream bucket_number_padded;
        bucket_number_padded << std::internal << std::setfill('0') << std::setw(3) << bucket_i;

        fs::path bucket_filename =
            fs::path(tmp_dirname) /
            fs::path(filename + L".sort_bucket_" + s2ws(bucket_number_padded.str()) + L".tmp");
        fs::remove(bucket_filename);
        this->bucket_files.push_back(FileDisk(bucket_filename));
    }
    this->final_position_start = 0;
    this->final_position_end = 0;
    this->next_bucket_to_sort = 0;
    this->entry_buf = new uint8_t[entry_size + 7]();
}

void b17SortManager::AddToCache(const Bits &entry)
{
    entry.ToBytes(this->entry_buf);
    return AddToCache(this->entry_buf);
}

void b17SortManager::AddToCache(const uint8_t *entry)
{
    if (this->done) {
        throw InvalidValueException("Already finished.");
    }
    uint64_t bucket_index =
        Util::ExtractNum(entry, this->entry_size, this->begin_bits, this->log_num_buckets);
    uint64_t mem_write_offset = mem_bucket_sizes[bucket_index] * entry_size;
    if (mem_write_offset + entry_size > this->size_per_bucket) {
        this->FlushTable(bucket_index);
        mem_write_offset = 0;
    }

    uint8_t *mem_write_pointer = mem_bucket_pointers[bucket_index] + mem_write_offset;
    memcpy(mem_write_pointer, entry, this->entry_size);
    mem_bucket_sizes[bucket_index] += 1;
}

uint8_t *b17SortManager::ReadEntry(uint64_t position, int quicksort /*= 0*/)
{
    if (position < this->final_position_start) {
        if (!(position >= this->prev_bucket_position_start)) {
            throw InvalidStateException("Invalid prev bucket start");
        }
        return (this->prev_bucket_buf + (position - this->prev_bucket_position_start));
    }

    while (position >= this->final_position_end) {
        SortBucket(quicksort);
    }
    if (!(this->final_position_end > position)) {
        throw InvalidValueException("Position too large");
    }
    if (!(this->final_position_start <= position)) {
        throw InvalidValueException("Position too small");
    }
    return this->memory_start + (position - this->final_position_start);
}

bool b17SortManager::CloseToNewBucket(uint64_t position) const
{
    if (!(position <= this->final_position_end)) {
        return this->next_bucket_to_sort < this->mem_bucket_pointers.size();
    };
    return (
        position + prev_bucket_buf_size / 2 >= this->final_position_end &&
        this->next_bucket_to_sort < this->mem_bucket_pointers.size());
}

void b17SortManager::TriggerNewBucket(uint64_t position, bool quicksort)
{
    if (!(position <= this->final_position_end)) {
        throw InvalidValueException("Triggering bucket too late");
    }
    if (!(position >= this->final_position_start)) {
        throw InvalidValueException("Triggering bucket too early");
    }

    // position is the first position that we need in the new array
    uint64_t cache_size = (this->final_position_end - position);
    memset(this->prev_bucket_buf, 0x00, this->prev_bucket_buf_size);
    memcpy(
        this->prev_bucket_buf,
        this->memory_start + position - this->final_position_start,
        cache_size);
    SortBucket(quicksort);
    this->prev_bucket_position_start = position;
}

void b17SortManager::ChangeMemory(uint8_t *new_memory, uint64_t new_memory_size)
{
    this->FlushCache();
    this->memory_start = new_memory;
    this->memory_size = new_memory_size;
    this->size_per_bucket = new_memory_size / this->mem_bucket_pointers.size();
    for (size_t bucket_i = 0; bucket_i < this->mem_bucket_pointers.size(); bucket_i++) {
        this->mem_bucket_pointers[bucket_i] = (new_memory + bucket_i * size_per_bucket);
    }
    this->final_position_start = 0;
    this->final_position_end = 0;
    this->next_bucket_to_sort = 0;
}

void b17SortManager::FlushCache()
{
    for (size_t bucket_i = 0; bucket_i < this->mem_bucket_pointers.size(); bucket_i++) {
        FlushTable(bucket_i);
    }
}

b17SortManager::~b17SortManager()
{
    // Close and delete files in case we exit without doing the sort
    for (auto &fd : this->bucket_files) {
        std::string filename = fd.GetFileName();
        fd.Close();
        fs::remove(fs::path(fd.GetFileName()));
    }
    delete[] this->prev_bucket_buf;
    delete[] this->entry_buf;
}

void b17SortManager::FlushTable(uint16_t bucket_i)
{
    uint64_t start_write = this->bucket_write_pointers[bucket_i];
    uint64_t write_len = this->mem_bucket_sizes[bucket_i] * this->entry_size;

    // Flush the bucket to disk
    bucket_files[bucket_i].Write(start_write, this->mem_bucket_pointers[bucket_i], write_len);
    this->bucket_write_pointers[bucket_i] += write_len;

    // Reset memory caches
    this->mem_bucket_pointers[bucket_i] = this->memory_start + bucket_i * this->size_per_bucket;
    this->mem_bucket_sizes[bucket_i] = 0;
}

void b17SortManager::SortBucket(int quicksort)
{
    this->done = true;
    if (next_bucket_to_sort >= this->mem_bucket_pointers.size()) {
        throw InvalidValueException("Trying to sort bucket which does not exist.");
    }
    uint64_t bucket_i = this->next_bucket_to_sort;
    uint64_t bucket_entries = bucket_write_pointers[bucket_i] / this->entry_size;
    uint64_t entries_fit_in_memory = this->memory_size / this->entry_size;

    uint32_t entry_len_memory = this->entry_size - this->begin_bits / 8;

    double have_ram = entry_size * entries_fit_in_memory / (1024.0 * 1024.0 * 1024.0);
    double qs_ram = entry_size * bucket_entries / (1024.0 * 1024.0 * 1024.0);
    double u_ram = Util::RoundSize(bucket_entries) * entry_len_memory / (1024.0 * 1024.0 * 1024.0);

    if (bucket_entries > entries_fit_in_memory) {
        throw InsufficientMemoryException(
            "Not enough memory for sort in memory. Need to sort " +
            std::to_string(this->bucket_write_pointers[bucket_i] / (1024.0 * 1024.0 * 1024.0)) +
            "GiB");
    }
    bool last_bucket = (bucket_i == this->mem_bucket_pointers.size() - 1) ||
                       this->bucket_write_pointers[bucket_i + 1] == 0;
    bool force_quicksort = (quicksort == 1) || (quicksort == 2 && last_bucket);
    // Do SortInMemory algorithm if it fits in the memory
    // (number of entries required * entry_len_memory) <= total memory available
    if (!force_quicksort &&
        Util::RoundSize(bucket_entries) * entry_len_memory <= this->memory_size) {
        std::cout << "\tBucket " << bucket_i << " uniform sort. Ram: " << std::fixed
                  << std::setprecision(3) << have_ram << "GiB, u_sort min: " << u_ram
                  << "GiB, qs min: " << qs_ram << "GiB." << std::endl;
        UniformSort::SortToMemory(
			context->pool,
            this->bucket_files[bucket_i],
            0,
            memory_start,
            this->entry_size,
            bucket_entries,
            this->begin_bits + this->log_num_buckets);
    } else {
        // Are we in Compress phrase 1 (quicksort=1) or is it the last bucket (quicksort=2)?
        // Perform quicksort if so (SortInMemory algorithm won't always perform well), or if we
        // don't have enough memory for uniform sort
        std::cout << "\tBucket " << bucket_i << " QS. Ram: " << std::fixed << std::setprecision(3)
                  << have_ram << "GiB, u_sort min: " << u_ram << "GiB, qs min: " << qs_ram
                  << "GiB. force_qs: " << force_quicksort << std::endl;
        this->bucket_files[bucket_i].Read(0, this->memory_start, bucket_entries * this->entry_size);
        QuickSort::Sort(this->memory_start, this->entry_size, bucket_entries, this->begin_bits);
    }

    // Deletes the bucket file
    std::string filename = this->bucket_files[bucket_i].GetFileName();
    this->bucket_files[bucket_i].Close();
    fs::remove(fs::path(filename));

    this->final_position_start = this->final_position_end;
    this->final_position_end += this->bucket_write_pointers[bucket_i];
    this->next_bucket_to_sort += 1;
}
