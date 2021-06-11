#include "phases.hpp"
#include "encoding.hpp"
#include "bitfield_index.hpp"

PlotEntry GetLeftEntry(
	uint8_t const table_index,
	uint8_t const* const left_buf,
	uint8_t const k,
	uint8_t const metadata_size,
	uint8_t const pos_size)
{
	PlotEntry left_entry;
	left_entry.y = 0;
	left_entry.read_posoffset = 0;
	left_entry.left_metadata = 0;
	left_entry.right_metadata = 0;

	uint32_t const ysize = (table_index == 7) ? k : k + kExtraBits;

	if (table_index == 1) {
		// For table 1, we only have y and metadata
		left_entry.y = SliceInt64FromBytes(left_buf, 0, k + kExtraBits);
		left_entry.left_metadata =
			SliceInt64FromBytes(left_buf, k + kExtraBits, metadata_size);
	} else {
		// For tables 2-6, we we also have pos and offset. We need to read this because
		// this entry will be written again to the table without the y (and some entries
		// are dropped).
		left_entry.y = SliceInt64FromBytes(left_buf, 0, ysize);
		left_entry.read_posoffset =
			SliceInt64FromBytes(left_buf, ysize, pos_size + kOffsetSize);
		if (metadata_size <= 128) {
			left_entry.left_metadata =
				SliceInt128FromBytes(left_buf, ysize + pos_size + kOffsetSize, metadata_size);
		} else {
			// Large metadatas that don't fit into 128 bits. (k > 32).
			left_entry.left_metadata =
				SliceInt128FromBytes(left_buf, ysize + pos_size + kOffsetSize, 128);
			left_entry.right_metadata = SliceInt128FromBytes(
				left_buf, ysize + pos_size + kOffsetSize + 128, metadata_size - 128);
		}
	}
	return left_entry;
}

void* F1thread(DiskPlotterContext* context, int const index, uint8_t const k, const uint8_t* id, std::mutex* smm)
{
	uint32_t const entry_size_bytes = 16;
	uint64_t const max_value = ((uint64_t)1 << (k));
	uint64_t const right_buf_entries = 1 << (kBatchSizes);

	std::unique_ptr<uint64_t[]> f1_entries(new uint64_t[(1U << kBatchSizes)]);

	F1Calculator f1(k, id);

	std::unique_ptr<uint8_t[]> right_writer_buf(new uint8_t[right_buf_entries * entry_size_bytes]);

	// Instead of computing f1(1), f1(2), etc, for each x, we compute them in batches
	// to increase CPU efficency.
	for (uint64_t lp = index; lp <= (((uint64_t)1) << (k - kBatchSizes));
		 lp = lp + context->globals.num_threads) {
		// For each pair x, y in the batch

		uint64_t right_writer_count = 0;
		uint64_t x = lp * (1 << (kBatchSizes));

		uint64_t const loopcount = std::min(max_value - x, (uint64_t)1 << (kBatchSizes));

		// Instead of computing f1(1), f1(2), etc, for each x, we compute them in batches
		// to increase CPU efficency.
		f1.CalculateBuckets(x, loopcount, f1_entries.get());
		for (uint32_t i = 0; i < loopcount; i++) {
			uint128_t entry;

			entry = (uint128_t)f1_entries[i] << (128 - kExtraBits - k);
			entry |= (uint128_t)x << (128 - kExtraBits - 2 * k);
			IntTo16Bytes(&right_writer_buf[i * entry_size_bytes], entry);
			right_writer_count++;
			x++;
		}

		std::lock_guard<std::mutex> l(*smm);

		// Write it out
		for (uint32_t i = 0; i < right_writer_count; i++) {
			context->globals.L_sort_manager->AddToCache(&(right_writer_buf[i * entry_size_bytes]));
		}
	}

	return 0;
}

void WriteParkToFile(
	TMemoCache& tmCache,
	Disk& final_disk,
	uint64_t table_start,
	uint64_t park_index,
	uint32_t park_size_bytes,
	uint128_t first_line_point,
	const std::vector<uint8_t>& park_deltas,
	const std::vector<uint64_t>& park_stubs,
	uint8_t k,
	uint8_t table_index,
	uint8_t* park_buffer,
	uint64_t const park_buffer_size)
{
	// Parks are fixed size, so we know where to start writing. The deltas will not go over
	// into the next park.
	uint64_t writer = table_start + park_index * park_size_bytes;
	uint8_t* index = park_buffer;

	first_line_point <<= 128 - 2 * k;
	IntTo16Bytes(index, first_line_point);
	index += EntrySizes::CalculateLinePointSize(k);

	// We use ParkBits instead of Bits since it allows storing more data
	ParkBits park_stubs_bits;
	for (uint64_t stub : park_stubs) {
		park_stubs_bits.AppendValue(stub, (k - kStubMinusBits));
	}
	uint32_t stubs_size = EntrySizes::CalculateStubsSize(k);
	uint32_t stubs_valid_size = cdiv(park_stubs_bits.GetSize(), 8);
	park_stubs_bits.ToBytes(index);
	memset(index + stubs_valid_size, 0, stubs_size - stubs_valid_size);
	index += stubs_size;

	// The stubs are random so they don't need encoding. But deltas are more likely to
	// be small, so we can compress them
	double R = kRValues[table_index - 1];
	uint8_t* deltas_start = index + 2;
	size_t deltas_size = Encoding::ANSEncodeDeltas(tmCache, park_deltas, R, deltas_start);

	if (!deltas_size) {
		// Uncompressed
		deltas_size = park_deltas.size();
		IntToTwoBytesLE(index, deltas_size | 0x8000);
		memcpy(deltas_start, park_deltas.data(), deltas_size);
	} else {
		// Compressed
		IntToTwoBytesLE(index, deltas_size);
	}

	index += 2 + deltas_size;

	if ((uint32_t)(index - park_buffer) > park_buffer_size) {
		std::cout << "index-park_buffer " << index - park_buffer << " park_buffer_size "
				  << park_buffer_size << std::endl;
		throw InvalidStateException(
			"Overflowed park buffer, writing " + std::to_string(index - park_buffer) +
			" bytes. Space: " + std::to_string(park_buffer_size));
	}
	memset(index, 0x00, park_size_bytes - (index - park_buffer));

	final_disk.Write(writer, (uint8_t*)park_buffer, park_size_bytes);
}

void* phase1_thread(DiskPlotterContext* context,THREADDATA* ptd)
{
	uint64_t const right_entry_size_bytes = ptd->right_entry_size_bytes;
	uint8_t const k = ptd->k;
	uint8_t const table_index = ptd->table_index;
	uint8_t const metadata_size = ptd->metadata_size;
	uint32_t const entry_size_bytes = ptd->entry_size_bytes;
	uint8_t const pos_size = ptd->pos_size;
	uint64_t const prevtableentries = ptd->prevtableentries;
	uint32_t const compressed_entry_size_bytes = ptd->compressed_entry_size_bytes;
	std::vector<FileDisk>* ptmp_1_disks = ptd->ptmp_1_disks;

	// Streams to read and right to tables. We will have handles to two tables. We will
	// read through the left table, compute matches, and evaluate f for matching entries,
	// writing results to the right table.
	uint64_t left_buf_entries = 5000 + (uint64_t)((1.1) * (context->globals.stripe_size));
	uint64_t right_buf_entries = 5000 + (uint64_t)((1.1) * (context->globals.stripe_size));
	std::unique_ptr<uint8_t[]> right_writer_buf(
		new uint8_t[right_buf_entries * right_entry_size_bytes + 7]);
	std::unique_ptr<uint8_t[]> left_writer_buf(
		new uint8_t[left_buf_entries * compressed_entry_size_bytes + 7]);

	FxCalculator f(k, table_index + 1);

	// Stores map of old positions to new positions (positions after dropping entries from L
	// table that did not match) Map ke
	uint16_t position_map_size = 2000;

	// Should comfortably fit 2 buckets worth of items
	std::unique_ptr<uint16_t[]> L_position_map(new uint16_t[position_map_size]);
	std::unique_ptr<uint16_t[]> R_position_map(new uint16_t[position_map_size]);

	// Start at left table pos = 0 and iterate through the whole table. Note that the left table
	// will already be sorted by y
	uint64_t totalstripes = (prevtableentries + context->globals.stripe_size - 1) / context->globals.stripe_size;
	uint64_t threadstripes = (totalstripes + context->globals.num_threads - 1) / context->globals.num_threads;

	for (uint64_t stripe = 0; stripe < threadstripes; stripe++) {
		uint64_t pos = (stripe * context->globals.num_threads + ptd->index) * context->globals.stripe_size;
		uint64_t const endpos = pos + context->globals.stripe_size + 1;  // one y value overlap
		uint64_t left_reader = pos * entry_size_bytes;
		uint64_t left_writer_count = 0;
		uint64_t stripe_left_writer_count = 0;
		uint64_t stripe_start_correction = 0xffffffffffffffff;
		uint64_t right_writer_count = 0;
		uint64_t matches = 0;  // Total matches

		// This is a sliding window of entries, since things in bucket i can match with things in
		// bucket
		// i + 1. At the end of each bucket, we find matches between the two previous buckets.
		std::vector<PlotEntry> bucket_L;
		std::vector<PlotEntry> bucket_R;

		uint64_t bucket = 0;
		bool end_of_table = false;  // We finished all entries in the left table

		uint64_t ignorebucket = 0xffffffffffffffff;
		bool bMatch = false;
		bool bFirstStripeOvertimePair = false;
		bool bSecondStripOvertimePair = false;
		bool bThirdStripeOvertimePair = false;

		bool bStripePregamePair = false;
		bool bStripeStartPair = false;
		bool need_new_bucket = false;
		bool first_thread = ptd->index % context->globals.num_threads == 0;
		bool last_thread = ptd->index % context->globals.num_threads == context->globals.num_threads - 1;

		uint64_t L_position_base = 0;
		uint64_t R_position_base = 0;
		uint64_t newlpos = 0;
		uint64_t newrpos = 0;
		std::vector<std::tuple<PlotEntry, PlotEntry, std::pair<Bits, Bits>>>
			current_entries_to_write;
		std::vector<std::tuple<PlotEntry, PlotEntry, std::pair<Bits, Bits>>>
			future_entries_to_write;
		std::vector<PlotEntry*> not_dropped;  // Pointers are stored to avoid copying entries

		if (pos == 0) {
			bMatch = true;
			bStripePregamePair = true;
			bStripeStartPair = true;
			stripe_left_writer_count = 0;
			stripe_start_correction = 0;
		}

		Sem::Wait(ptd->theirs);
		need_new_bucket = context->globals.L_sort_manager->CloseToNewBucket(left_reader);
		if (need_new_bucket) {
			if (!first_thread) {
				Sem::Wait(ptd->theirs);
			}
			context->globals.L_sort_manager->TriggerNewBucket(left_reader);
		}
		if (!last_thread) {
			// Do not post if we are the last thread, because first thread has already
			// waited for us to finish when it starts
			Sem::Post(ptd->mine);
		}

		while (pos < prevtableentries + 1) {
			PlotEntry left_entry = PlotEntry();
			if (pos >= prevtableentries) {
				end_of_table = true;
				left_entry.y = 0;
				left_entry.left_metadata = 0;
				left_entry.right_metadata = 0;
				left_entry.used = false;
			} else {
				// Reads a left entry from disk
				uint8_t* left_buf = context->globals.L_sort_manager->ReadEntry(left_reader);
				left_reader += entry_size_bytes;

				left_entry = GetLeftEntry(table_index, left_buf, k, metadata_size, pos_size);
			}

			// This is not the pos that was read from disk,but the position of the entry we read,
			// within L table.
			left_entry.pos = pos;
			left_entry.used = false;
			uint64_t y_bucket = left_entry.y / kBC;

			if (!bMatch) {
				if (ignorebucket == 0xffffffffffffffff) {
					ignorebucket = y_bucket;
				} else {
					if ((y_bucket != ignorebucket)) {
						bucket = y_bucket;
						bMatch = true;
					}
				}
			}
			if (!bMatch) {
				stripe_left_writer_count++;
				R_position_base = stripe_left_writer_count;
				pos++;
				continue;
			}

			// Keep reading left entries into bucket_L and R, until we run out of things
			if (y_bucket == bucket) {
				bucket_L.emplace_back(left_entry);
			} else if (y_bucket == bucket + 1) {
				bucket_R.emplace_back(left_entry);
			} else {
				// cout << "matching! " << bucket << " and " << bucket + 1 << endl;
				// This is reached when we have finished adding stuff to bucket_R and bucket_L,
				// so now we can compare entries in both buckets to find matches. If two entries
				// match, match, the result is written to the right table. However the writing
				// happens in the next iteration of the loop, since we need to remap positions.
				uint16_t idx_L[10000];
				uint16_t idx_R[10000];
				int32_t idx_count = 0;

				if (!bucket_L.empty()) {
					not_dropped.clear();

					if (!bucket_R.empty()) {
						// Compute all matches between the two buckets and save indeces.
						idx_count = f.FindMatches(bucket_L, bucket_R, idx_L, idx_R);
						if (idx_count >= 10000) {
							std::cout << "sanity check: idx_count exceeded 10000!" << std::endl;
							exit(0);
						}
						// We mark entries as used if they took part in a match.
						for (int32_t i = 0; i < idx_count; i++) {
							bucket_L[idx_L[i]].used = true;
							if (end_of_table) {
								bucket_R[idx_R[i]].used = true;
							}
						}
					}

					// Adds L_bucket entries that are used to not_dropped. They are used if they
					// either matched with something to the left (in the previous iteration), or
					// matched with something in bucket_R (in this iteration).
					for (size_t bucket_index = 0; bucket_index < bucket_L.size(); bucket_index++) {
						PlotEntry& L_entry = bucket_L[bucket_index];
						if (L_entry.used) {
							not_dropped.emplace_back(&bucket_L[bucket_index]);
						}
					}
					if (end_of_table) {
						// In the last two buckets, we will not get a chance to enter the next
						// iteration due to breaking from loop. Therefore to write the final
						// bucket in this iteration, we have to add the R entries to the
						// not_dropped list.
						for (size_t bucket_index = 0; bucket_index < bucket_R.size();
							 bucket_index++) {
							PlotEntry& R_entry = bucket_R[bucket_index];
							if (R_entry.used) {
								not_dropped.emplace_back(&R_entry);
							}
						}
					}
					// We keep maps from old positions to new positions. We only need two maps,
					// one for L bucket and one for R bucket, and we cycle through them. Map
					// keys are stored as positions % 2^10 for efficiency. Map values are stored
					// as offsets from the base position for that bucket, for efficiency.
					std::swap(L_position_map, R_position_map);
					L_position_base = R_position_base;
					R_position_base = stripe_left_writer_count;

					for (PlotEntry*& entry : not_dropped) {
						// The new position for this entry = the total amount of thing written
						// to L so far. Since we only write entries in not_dropped, about 14% of
						// entries are dropped.
						R_position_map[entry->pos % position_map_size] =
							stripe_left_writer_count - R_position_base;

						if (bStripeStartPair) {
							if (stripe_start_correction == 0xffffffffffffffff) {
								stripe_start_correction = stripe_left_writer_count;
							}

							if (left_writer_count >= left_buf_entries) {
								throw InvalidStateException("Left writer count overrun");
							}
							uint8_t* tmp_buf = left_writer_buf.get() +
											   left_writer_count * compressed_entry_size_bytes;

							left_writer_count++;
							// memset(tmp_buf, 0xff, compressed_entry_size_bytes);

							// Rewrite left entry with just pos and offset, to reduce working space
							uint64_t new_left_entry;
							if (table_index == 1)
								new_left_entry = entry->left_metadata;
							else
								new_left_entry = entry->read_posoffset;
							new_left_entry <<= 64 - (table_index == 1 ? k : pos_size + kOffsetSize);
							IntToEightBytes(tmp_buf, new_left_entry);
						}
						stripe_left_writer_count++;
					}

					// Two vectors to keep track of things from previous iteration and from this
					// iteration.
					current_entries_to_write = std::move(future_entries_to_write);
					future_entries_to_write.clear();

					for (int32_t i = 0; i < idx_count; i++) {
						PlotEntry& L_entry = bucket_L[idx_L[i]];
						PlotEntry& R_entry = bucket_R[idx_R[i]];

						if (bStripeStartPair)
							matches++;

						// Sets the R entry to used so that we don't drop in next iteration
						R_entry.used = true;
						// Computes the output pair (fx, new_metadata)
						if (metadata_size <= 128) {
							const std::pair<Bits, Bits>& f_output = f.CalculateBucket(
								Bits(L_entry.y, k + kExtraBits),
								Bits(L_entry.left_metadata, metadata_size),
								Bits(R_entry.left_metadata, metadata_size));
							future_entries_to_write.emplace_back(L_entry, R_entry, f_output);
						} else {
							// Metadata does not fit into 128 bits
							const std::pair<Bits, Bits>& f_output = f.CalculateBucket(
								Bits(L_entry.y, k + kExtraBits),
								Bits(L_entry.left_metadata, 128) +
									Bits(L_entry.right_metadata, metadata_size - 128),
								Bits(R_entry.left_metadata, 128) +
									Bits(R_entry.right_metadata, metadata_size - 128));
							future_entries_to_write.emplace_back(L_entry, R_entry, f_output);
						}
					}

					// At this point, future_entries_to_write contains the matches of buckets L
					// and R, and current_entries_to_write contains the matches of L and the
					// bucket left of L. These are the ones that we will write.
					uint16_t final_current_entry_size = current_entries_to_write.size();
					if (end_of_table) {
						// For the final bucket, write the future entries now as well, since we
						// will break from loop
						current_entries_to_write.insert(
							current_entries_to_write.end(),
							future_entries_to_write.begin(),
							future_entries_to_write.end());
					}
					for (size_t i = 0; i < current_entries_to_write.size(); i++) {
						const auto& [L_entry, R_entry, f_output] = current_entries_to_write[i];

						// We only need k instead of k + kExtraBits bits for the last table
						Bits new_entry = table_index + 1 == 7 ? std::get<0>(f_output).Slice(0, k)
															  : std::get<0>(f_output);

						// Maps the new positions. If we hit end of pos, we must write things in
						// both final_entries to write and current_entries_to_write, which are
						// in both position maps.
						if (!end_of_table || i < final_current_entry_size) {
							newlpos =
								L_position_map[L_entry.pos % position_map_size] + L_position_base;
						} else {
							newlpos =
								R_position_map[L_entry.pos % position_map_size] + R_position_base;
						}
						newrpos = R_position_map[R_entry.pos % position_map_size] + R_position_base;
						// Position in the previous table
						new_entry.AppendValue(newlpos, pos_size);

						// Offset for matching entry
						if (newrpos - newlpos > (1U << kOffsetSize) * 97 / 100) {
							throw InvalidStateException(
								"Offset too large: " + std::to_string(newrpos - newlpos));
						}

						new_entry.AppendValue(newrpos - newlpos, kOffsetSize);
						// New metadata which will be used to compute the next f
						new_entry += std::get<1>(f_output);

						if (right_writer_count >= right_buf_entries) {
							throw InvalidStateException("Left writer count overrun");
						}

						if (bStripeStartPair) {
							uint8_t* right_buf = right_writer_buf.get() +
												 right_writer_count * right_entry_size_bytes;
							new_entry.ToBytes(right_buf);
							right_writer_count++;
						}
					}
				}

				if (pos >= endpos) {
					if (!bFirstStripeOvertimePair)
						bFirstStripeOvertimePair = true;
					else if (!bSecondStripOvertimePair)
						bSecondStripOvertimePair = true;
					else if (!bThirdStripeOvertimePair)
						bThirdStripeOvertimePair = true;
					else {
						break;
					}
				} else {
					if (!bStripePregamePair)
						bStripePregamePair = true;
					else if (!bStripeStartPair)
						bStripeStartPair = true;
				}

				if (y_bucket == bucket + 2) {
					// We saw a bucket that is 2 more than the current, so we just set L = R, and R
					// = [entry]
					bucket_L = std::move(bucket_R);
					bucket_R.clear();
					bucket_R.emplace_back(std::move(left_entry));
					++bucket;
				} else {
					// We saw a bucket that >2 more than the current, so we just set L = [entry],
					// and R = []
					bucket = y_bucket;
					bucket_L.clear();
					bucket_L.emplace_back(std::move(left_entry));
					bucket_R.clear();
				}
			}
			// Increase the read pointer in the left table, by one
			++pos;
		}

		// If we needed new bucket, we already waited
		// Do not wait if we are the first thread, since we are guaranteed that everything is
		// written
		if (!need_new_bucket && !first_thread) {
			Sem::Wait(ptd->theirs);
		}

		uint32_t const ysize = (table_index + 1 == 7) ? k : k + kExtraBits;
		uint32_t const startbyte = ysize / 8;
		uint32_t const endbyte = (ysize + pos_size + 7) / 8 - 1;
		uint64_t const shiftamt = (8 - ((ysize + pos_size) % 8)) % 8;
		uint64_t const correction = (context->globals.left_writer_count - stripe_start_correction)
									<< shiftamt;

		// Correct positions
		for (uint32_t i = 0; i < right_writer_count; i++) {
			uint64_t posaccum = 0;
			uint8_t* entrybuf = right_writer_buf.get() + i * right_entry_size_bytes;

			for (uint32_t j = startbyte; j <= endbyte; j++) {
				posaccum = (posaccum << 8) | (entrybuf[j]);
			}
			posaccum += correction;
			for (uint32_t j = endbyte; j >= startbyte; --j) {
				entrybuf[j] = posaccum & 0xff;
				posaccum = posaccum >> 8;
			}
		}
		if (table_index < 6) {
			for (uint64_t i = 0; i < right_writer_count; i++) {
				context->globals.R_sort_manager->AddToCache(
					right_writer_buf.get() + i * right_entry_size_bytes);
			}
		} else {
			// Writes out the right table for table 7
			(*ptmp_1_disks)[table_index + 1].Write(
				context->globals.right_writer,
				right_writer_buf.get(),
				right_writer_count * right_entry_size_bytes);
		}
		context->globals.right_writer += right_writer_count * right_entry_size_bytes;
		context->globals.right_writer_count += right_writer_count;

		(*ptmp_1_disks)[table_index].Write(
			context->globals.left_writer,
			left_writer_buf.get(),
			left_writer_count * compressed_entry_size_bytes);
		context->globals.left_writer += left_writer_count * compressed_entry_size_bytes;
		context->globals.left_writer_count += left_writer_count;

		context->globals.matches += matches;
		Sem::Post(ptd->mine);
	}

	return 0;
}

std::vector<uint64_t> RunPhase1(
	DiskPlotterContext* context,
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
	bool show_progress)
{
	std::cout << "Computing table 1" << std::endl;
	context->globals.stripe_size = stripe_size;
	context->globals.num_threads = num_threads;
	Timer f1_start_time;
	F1Calculator f1(k, id);
	uint64_t x = 0;

	uint32_t const t1_entry_size_bytes = EntrySizes::GetMaxEntrySize(k, 1, true);
	context->globals.L_sort_manager = std::make_unique<SortManager>(
		context,
		memory_size,
		num_buckets,
		log_num_buckets,
		t1_entry_size_bytes,
		tmp_dirname,
		filename + L".p1.t1",
		0,
		context->globals.stripe_size);

	// These are used for sorting on disk. The sort on disk code needs to know how
	// many elements are in each bucket.
	std::vector<uint64_t> table_sizes = std::vector<uint64_t>(8, 0);
	std::mutex sort_manager_mutex;

	{
		// Start of parallel execution
		std::vector<std::thread> threads;
		for (int i = 0; i < num_threads; i++) {
			threads.emplace_back(F1thread,context, i, k, id, &sort_manager_mutex);
		}

		for (auto& t : threads) {
			t.join();
		}
		// end of parallel execution
	}

	uint64_t prevtableentries = 1ULL << k;
	f1_start_time.PrintElapsed("F1 complete, time:");
	context->globals.L_sort_manager->FlushCache();
	table_sizes[1] = x + 1;

	// Store positions to previous tables, in k bits.
	uint8_t pos_size = k;
	uint32_t right_entry_size_bytes = 0;

	FxCalculator f(k, 1);

	//// For tables 1 through 6, sort the table, calculate matches, and write
	//// the next table. This is the left table index.
	for (uint8_t table_index = 1; table_index < 7; table_index++) {
		Timer table_timer;
		uint8_t const metadata_size = kVectorLens[table_index + 1] * k;

		// Determines how many bytes the entries in our left and right tables will take up.
		uint32_t const entry_size_bytes = EntrySizes::GetMaxEntrySize(k, table_index, true);
		uint32_t compressed_entry_size_bytes = EntrySizes::GetMaxEntrySize(k, table_index, false);
		right_entry_size_bytes = EntrySizes::GetMaxEntrySize(k, table_index + 1, true);

		if (flags & ENABLE_BITFIELD && table_index != 1) {
			// We only write pos and offset to tables 2-6 after removing
			// metadata
			compressed_entry_size_bytes = cdiv(k + kOffsetSize, 8);
			if (table_index == 6) {
				// Table 7 will contain f7, pos and offset
				right_entry_size_bytes = EntrySizes::GetKeyPosOffsetSize(k);
			}
		}

		std::cout << "Computing table " << int{table_index + 1} << std::endl;
		// Start of parallel execution

		//FxCalculator f(k, table_index + 1);  // dummy to load static table
		//moved

		context->globals.matches = 0;
		context->globals.left_writer_count = 0;
		context->globals.right_writer_count = 0;
		context->globals.right_writer = 0;
		context->globals.left_writer = 0;

		context->globals.R_sort_manager = std::make_unique<SortManager>(
			context,
			memory_size,
			num_buckets,
			log_num_buckets,
			right_entry_size_bytes,
			tmp_dirname,
			filename + L".p1.t" + std::to_wstring(table_index + 1),
			0,
			context->globals.stripe_size);

		context->globals.L_sort_manager->TriggerNewBucket(0);

		Timer computation_pass_timer;

		auto td = std::make_unique<THREADDATA[]>(num_threads);
		auto mutex = std::make_unique<Sem::type[]>(num_threads);

		std::vector<std::thread> threads;

		for (int i = 0; i < num_threads; i++) {
			mutex[i] = Sem::Create();
		}

		for (int i = 0; i < num_threads; i++) {
			td[i].index = i;
			td[i].mine = &mutex[i];
			td[i].theirs = &mutex[(num_threads + i - 1) % num_threads];

			td[i].prevtableentries = prevtableentries;
			td[i].right_entry_size_bytes = right_entry_size_bytes;
			td[i].k = k;
			td[i].table_index = table_index;
			td[i].metadata_size = metadata_size;
			td[i].entry_size_bytes = entry_size_bytes;
			td[i].pos_size = pos_size;
			td[i].compressed_entry_size_bytes = compressed_entry_size_bytes;
			td[i].ptmp_1_disks = &tmp_1_disks;

			threads.emplace_back(phase1_thread, context,&td[i]);
		}
		Sem::Post(&mutex[num_threads - 1]);

		for (auto& t : threads) {
			t.join();
		}

		for (int i = 0; i < num_threads; i++) {
			Sem::Destroy(mutex[i]);
		}

		// end of parallel execution

		// Total matches found in the left table
		std::cout << "\tTotal matches: " << context->globals.matches << std::endl;

		table_sizes[table_index] = context->globals.left_writer_count;
		table_sizes[table_index + 1] = context->globals.right_writer_count;

		// Truncates the file after the final write position, deleting no longer useful
		// working space
		tmp_1_disks[table_index].Truncate(context->globals.left_writer);
		context->globals.L_sort_manager.reset();
		if (table_index < 6) {
			context->globals.R_sort_manager->FlushCache();
			context->globals.L_sort_manager = std::move(context->globals.R_sort_manager);
		} else {
			tmp_1_disks[table_index + 1].Truncate(context->globals.right_writer);
		}

		// Resets variables
		if (context->globals.matches != context->globals.right_writer_count) {
			throw InvalidStateException(
				"Matches do not match with number of write entries " +
				std::to_string(context->globals.matches) + " " + std::to_string(context->globals.right_writer_count));
		}

		prevtableentries = context->globals.right_writer_count;
		table_timer.PrintElapsed("Forward propagation table time:");
		//if ((flags & SHOW_PROGRESS) || show_progress) {
		//	progress(1, table_index, 6);
		//}
	}
	table_sizes[0] = 0;
	context->globals.R_sort_manager.reset();
	return table_sizes;
}

Phase2Results RunPhase2(
	DiskPlotterContext* context,
	std::vector<FileDisk>& tmp_1_disks,
	std::vector<uint64_t> table_sizes,
	uint8_t const k,
	const uint8_t* id,
	const std::wstring& tmp_dirname,
	const std::wstring& filename,
	uint64_t memory_size,
	uint32_t const num_buckets,
	uint32_t const log_num_buckets,
	uint8_t const flags)
{
	// After pruning each table will have 0.865 * 2^k or fewer entries on
	// average
	uint8_t const pos_size = k;
	uint8_t const pos_offset_size = pos_size + kOffsetSize;
	uint8_t const write_counter_shift = 128 - k;
	uint8_t const pos_offset_shift = write_counter_shift - pos_offset_size;
	uint8_t const f7_shift = 128 - k;
	uint8_t const t7_pos_offset_shift = f7_shift - pos_offset_size;
	uint8_t const new_entry_size = EntrySizes::GetKeyPosOffsetSize(k);

	std::vector<uint64_t> new_table_sizes(8, 0);
	new_table_sizes[7] = table_sizes[7];

	// Iterates through each table, starting at 6 & 7. Each iteration, we scan
	// the current table twice. In the first scan, we:

	// 1. drop entries marked as false in the current bitfield (except table 7,
	//    where we don't drop anything, this is a special case)
	// 2. mark entries in the next_bitfield that non-dropped entries have
	//    references to

	// The second scan of the table, we update the positions and offsets to
	// reflect the entries that will be dropped in the next table.

	// At the end of the iteration, we transfer the next_bitfield to the current bitfield
	// to use it to prune the next table to scan.

	int64_t const max_table_size = *std::max_element(table_sizes.begin(), table_sizes.end());

	bitfield next_bitfield(max_table_size);
	bitfield current_bitfield(max_table_size);

	std::vector<std::unique_ptr<SortManager>> output_files;

	// table 1 and 7 are special. They are passed on as plain files on disk.
	// Only table 2-6 are passed on as SortManagers, to phase3
	output_files.resize(7 - 2);

	// note that we don't iterate over table_index=1. That table is special
	// since it contains different data. We'll do an extra scan of table 1 at
	// the end, just to compact it.
	for (int table_index = 7; table_index > 1; --table_index) {
		std::cout << "Backpropagating on table " << table_index << std::endl;

		Timer scan_timer;

		next_bitfield.clear();

		int64_t const table_size = table_sizes[table_index];
		int16_t const entry_size = cdiv(k + kOffsetSize + (table_index == 7 ? k : 0), 8);

		BufferedDisk disk(&tmp_1_disks[table_index], table_size * entry_size);

		// read_index is the number of entries we've processed so far (in the
		// current table) i.e. the index to the current entry. This is not used
		// for table 7

		int64_t read_cursor = 0;
		for (int64_t read_index = 0; read_index < table_size;
			 ++read_index, read_cursor += entry_size) {
			uint8_t const* entry = disk.Read(read_cursor, entry_size);

			uint64_t entry_pos_offset = 0;
			if (table_index == 7) {
				// table 7 is special, we never drop anything, so just build
				// next_bitfield
				entry_pos_offset = SliceInt64FromBytes(entry, k, pos_offset_size);
			} else {
				if (!current_bitfield.get(read_index)) {
					// This entry should be dropped.
					continue;
				}
				entry_pos_offset = SliceInt64FromBytes(entry, 0, pos_offset_size);
			}

			uint64_t entry_pos = entry_pos_offset >> kOffsetSize;
			uint64_t entry_offset = entry_pos_offset & ((1U << kOffsetSize) - 1);
			// mark the two matching entries as used (pos and pos+offset)
			next_bitfield.set(entry_pos);
			next_bitfield.set(entry_pos + entry_offset);
		}

		std::cout << "scanned table " << table_index << std::endl;
		scan_timer.PrintElapsed("scanned time = ");

		std::cout << "sorting table " << table_index << std::endl;
		Timer sort_timer;

		// read the same table again. This time we'll output it to new files:
		// * add sort_key (just the index of the current entry)
		// * update (pos, offset) to remain valid after table_index-1 has been
		//   compacted.
		// * sort by pos
		//
		// As we have to sort two adjacent tables at the same time in phase 3,
		// we can use only a half of memory_size for SortManager. However,
		// table 1 is already sorted, so we can use all memory for sorting
		// table 2.

		auto sort_manager = std::make_unique<SortManager>(
			context,
			table_index == 2 ? memory_size : memory_size / 2,
			num_buckets,
			log_num_buckets,
			new_entry_size,
			tmp_dirname,
			filename + L".p2.t" + std::to_wstring(table_index),
			uint32_t(k),
			0,
			strategy_t::quicksort_last,
			memory_size);

		// as we scan the table for the second time, we'll also need to remap
		// the positions and offsets based on the next_bitfield.
		bitfield_index const index(next_bitfield);

		read_cursor = 0;
		int64_t write_counter = 0;
		for (int64_t read_index = 0; read_index < table_size;
			 ++read_index, read_cursor += entry_size) {
			uint8_t const* entry = disk.Read(read_cursor, entry_size);

			uint64_t entry_f7 = 0;
			uint64_t entry_pos_offset;
			if (table_index == 7) {
				// table 7 is special, we never drop anything, so just build
				// next_bitfield
				entry_f7 = SliceInt64FromBytes(entry, 0, k);
				entry_pos_offset = SliceInt64FromBytes(entry, k, pos_offset_size);
			} else {
				// skipping
				if (!current_bitfield.get(read_index))
					continue;

				entry_pos_offset = SliceInt64FromBytes(entry, 0, pos_offset_size);
			}

			uint64_t entry_pos = entry_pos_offset >> kOffsetSize;
			uint64_t entry_offset = entry_pos_offset & ((1U << kOffsetSize) - 1);

			// assemble the new entry and write it to the sort manager

			// map the pos and offset to the new, compacted, positions and
			// offsets
			std::tie(entry_pos, entry_offset) = index.lookup(entry_pos, entry_offset);
			entry_pos_offset = (entry_pos << kOffsetSize) | entry_offset;

			uint8_t bytes[16];
			if (table_index == 7) {
				// table 7 is already sorted by pos, so we just rewrite the
				// pos and offset in-place
				uint128_t new_entry = (uint128_t)entry_f7 << f7_shift;
				new_entry |= (uint128_t)entry_pos_offset << t7_pos_offset_shift;
				IntTo16Bytes(bytes, new_entry);

				disk.Write(read_index * entry_size, bytes, entry_size);
			} else {
				// The new entry is slightly different. Metadata is dropped, to
				// save space, and the counter of the entry is written (sort_key). We
				// use this instead of (y + pos + offset) since its smaller.
				uint128_t new_entry = (uint128_t)write_counter << write_counter_shift;
				new_entry |= (uint128_t)entry_pos_offset << pos_offset_shift;
				IntTo16Bytes(bytes, new_entry);

				sort_manager->AddToCache(bytes);
			}
			++write_counter;
		}

		if (table_index != 7) {
			sort_manager->FlushCache();
			sort_timer.PrintElapsed("sort time = ");

			// clear disk caches
			disk.FreeMemory();
			sort_manager->FreeMemory();

			output_files[table_index - 2] = std::move(sort_manager);
			new_table_sizes[table_index] = write_counter;
		}
		current_bitfield.swap(next_bitfield);
		next_bitfield.clear();

		// The files for Table 1 and 7 are re-used, overwritten and passed on to
		// the next phase. However, table 2 through 6 are all written to sort
		// managers that are passed on to the next phase. At this point, we have
		// to delete the input files for table 2-6 to save disk space.
		// This loop doesn't cover table 1, it's handled below with the
		// FilteredDisk wrapper.
		if (table_index != 7) {
			tmp_1_disks[table_index].Truncate(0);
		}
		//if (flags & SHOW_PROGRESS) {
		//	progress(2, 8 - table_index, 6);
		//}
	}

	// lazy-compact table 1 based on current_bitfield

	int const table_index = 1;
	int64_t const table_size = table_sizes[table_index];
	int16_t const entry_size = EntrySizes::GetMaxEntrySize(k, table_index, false);

	// at this point, table 1 still needs to be compacted, based on
	// current_bitfield. Instead of compacting it right now, defer it and read
	// from it as-if it was compacted. This saves one read and one write pass
	new_table_sizes[table_index] = current_bitfield.count(0, table_size);
	BufferedDisk disk(&tmp_1_disks[table_index], table_size * entry_size);

	std::cout << "table " << table_index << " new size: " << new_table_sizes[table_index]
			  << std::endl;

	return {
		FilteredDisk(std::move(disk), std::move(current_bitfield), entry_size),
		BufferedDisk(&tmp_1_disks[7], new_table_sizes[7] * new_entry_size),
		std::move(output_files),
		std::move(new_table_sizes)};
}

Disk& Phase2Results::disk_for_table(int const table_index)
{
	if (table_index == 1)
		return table1;
	else if (table_index == 7)
		return table7;
	else
		return *output_files[table_index - 2];
}

Phase3Results RunPhase3(
	DiskPlotterContext* context,
	uint8_t k,
	FileDisk& tmp2_disk /*filename*/,
	Phase2Results res2,
	const uint8_t* id,
	const std::wstring& tmp_dirname,
	const std::wstring& filename,
	uint32_t header_size,
	uint64_t memory_size,
	uint32_t num_buckets,
	uint32_t log_num_buckets,
	const uint8_t flags)
{
	uint8_t const pos_size = k;
	uint8_t const line_point_size = 2 * k - 1;

	// file size is not really know at this point, but it doesn't matter as
	// we're only writing
	BufferedDisk tmp2_buffered_disk(&tmp2_disk, 0);

	std::vector<uint64_t> final_table_begin_pointers(12, 0);
	final_table_begin_pointers[1] = header_size;

	uint8_t table_pointer_bytes[8];
	IntToEightBytes(table_pointer_bytes, final_table_begin_pointers[1]);
	tmp2_disk.Write(header_size - 10 * 8, table_pointer_bytes, 8);

	uint64_t final_entries_written = 0;
	uint32_t right_entry_size_bytes = 0;
	uint32_t new_pos_entry_size_bytes = 0;

	std::unique_ptr<SortManager> L_sort_manager;
	std::unique_ptr<SortManager> R_sort_manager;

	// These variables are used in the WriteParkToFile method. They are preallocatted here
	// to save time.
	uint64_t const park_buffer_size = EntrySizes::CalculateLinePointSize(k) +
									  EntrySizes::CalculateStubsSize(k) + 2 +
									  EntrySizes::CalculateMaxDeltasSize(k, 1);
	std::unique_ptr<uint8_t[]> park_buffer(new uint8_t[park_buffer_size]);

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

		Disk& right_disk = res2.disk_for_table(table_index + 1);
		Disk& left_disk = res2.disk_for_table(table_index);

		// Sort key is k bits for all tables. For table 7 it is just y, which
		// is k bits, and for all other tables the number of entries does not
		// exceed 0.865 * 2^k on average.
		uint32_t right_sort_key_size = k;

		uint32_t left_entry_size_bytes = EntrySizes::GetMaxEntrySize(k, table_index, false);
		uint32_t p2_entry_size_bytes = EntrySizes::GetKeyPosOffsetSize(k);
		right_entry_size_bytes = EntrySizes::GetMaxEntrySize(k, table_index + 1, false);

		uint64_t left_reader = 0;
		uint64_t right_reader = 0;
		uint64_t left_reader_count = 0;
		uint64_t right_reader_count = 0;
		uint64_t total_r_entries = 0;

		if (table_index > 1) {
			L_sort_manager->FreeMemory();
		}

		// We read only from this SortManager during the second pass, so all
		// memory is available
		R_sort_manager = std::make_unique<SortManager>(
			context,
			memory_size,
			num_buckets,
			log_num_buckets,
			right_entry_size_bytes,
			tmp_dirname,
			filename + L".p3.t" + std::to_wstring(table_index + 1),
			0,
			0,
			strategy_t::quicksort_last);

		bool should_read_entry = true;
		std::vector<uint64_t> left_new_pos(kCachedPositionsSize);

		uint64_t old_sort_keys[kReadMinusWrite][kMaxMatchesSingleEntry];
		uint64_t old_offsets[kReadMinusWrite][kMaxMatchesSingleEntry];
		uint16_t old_counters[kReadMinusWrite];
		for (uint16_t& old_counter : old_counters) {
			old_counter = 0;
		}
		bool end_of_right_table = false;
		uint64_t current_pos = 0;
		uint64_t end_of_table_pos = 0;
		uint64_t greatest_pos = 0;

		uint8_t const* left_entry_disk_buf = nullptr;

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
						if (right_reader_count == res2.table_sizes[table_index + 1]) {
							end_of_right_table = true;
							end_of_table_pos = current_pos;
							right_disk.FreeMemory();
							break;
						}
						// The right entries are in the format from backprop, (sort_key, pos,
						// offset)
						uint8_t const* right_entry_buf =
							right_disk.Read(right_reader, p2_entry_size_bytes);
						right_reader += p2_entry_size_bytes;
						right_reader_count++;

						entry_sort_key =
							SliceInt64FromBytes(right_entry_buf, 0, right_sort_key_size);
						entry_pos = SliceInt64FromBytes(
							right_entry_buf, right_sort_key_size, pos_size);
						entry_offset = SliceInt64FromBytes(
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
						uint64_t const old_write_pos = entry_pos % kReadMinusWrite;
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

				if (left_reader_count < res2.table_sizes[table_index]) {
					// The left entries are in the new format: (sort_key, new_pos), except for table
					// 1: (y, x).

					// TODO: unify these cases once SortManager implements
					// the ReadDisk interface
					if (table_index == 1) {
						left_entry_disk_buf = left_disk.Read(left_reader, left_entry_size_bytes);
						left_reader += left_entry_size_bytes;
					} else {
						left_entry_disk_buf = L_sort_manager->ReadEntry(left_reader);
						left_reader += new_pos_entry_size_bytes;
					}
					left_reader_count++;
				}

				// We read the "new_pos" from the L table, which for table 1 is just x. For
				// other tables, the new_pos
				if (table_index == 1) {
					// Only k bits, since this is x
					left_new_pos[current_pos % kCachedPositionsSize] =
						SliceInt64FromBytes(left_entry_disk_buf, 0, k);
				} else {
					// k+1 bits in case it overflows
					left_new_pos[current_pos % kCachedPositionsSize] =
						SliceInt64FromBytes(left_entry_disk_buf, right_sort_key_size, k);
				}
			}

			uint64_t const write_pointer_pos = current_pos - kReadMinusWrite + 1;

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
					to_write.AppendValue(
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
		left_disk.Truncate(0);

		// Flush cache so all entries are written to buckets
		R_sort_manager->FlushCache();
		R_sort_manager->FreeMemory();

		Timer computation_pass_2_timer;

		right_reader = 0;
		right_reader_count = 0;
		uint64_t final_table_writer = final_table_begin_pointers[table_index];

		final_entries_written = 0;

		if (table_index > 1) {
			// Make sure all files are removed
			L_sort_manager.reset();
		}

		// In the second pass we read from R sort manager and write to L sort
		// manager, and they both handle table (table_index + 1)'s data. The
		// newly written table consists of (sort_key, new_pos). Add one extra
		// bit for 'new_pos' to the 7-th table as it may have more than 2^k
		// entries.
		new_pos_entry_size_bytes = cdiv(2 * k + (table_index == 6 ? 1 : 0), 8);

		// For tables below 6 we can only use a half of memory_size since it
		// will be sorted in the first pass of the next iteration together with
		// the next table, which will use the other half of memory_size.
		// Tables 6 and 7 will be sorted alone, so we use all memory for them.
		L_sort_manager = std::make_unique<SortManager>(
			context,
			(table_index >= 5) ? memory_size : (memory_size / 2),
			num_buckets,
			log_num_buckets,
			new_pos_entry_size_bytes,
			tmp_dirname,
			filename + L".p3s.t" + std::to_wstring(table_index + 1),
			0,
			0,
			strategy_t::quicksort_last,
			memory_size);

		std::vector<uint8_t> park_deltas;
		std::vector<uint64_t> park_stubs;
		uint128_t checkpoint_line_point = 0;
		uint128_t last_line_point = 0;
		uint64_t park_index = 0;

		uint8_t* right_reader_entry_buf;

		// Now we will write on of the final tables, since we have a table sorted by line point.
		// The final table will simply store the deltas between each line_point, in fixed space
		// groups(parks), with a checkpoint in each group.
		int added_to_cache = 0;
		uint8_t const sort_key_shift = 128 - right_sort_key_size;
		uint8_t const index_shift = sort_key_shift - (k + (table_index == 6 ? 1 : 0));
		for (uint64_t index = 0; index < total_r_entries; index++) {
			right_reader_entry_buf = R_sort_manager->ReadEntry(right_reader);
			right_reader += right_entry_size_bytes;
			right_reader_count++;

			// Right entry is read as (line_point, sort_key)
			uint128_t line_point =
				SliceInt128FromBytes(right_reader_entry_buf, 0, line_point_size);
			uint64_t sort_key = SliceInt64FromBytes(
				right_reader_entry_buf, line_point_size, right_sort_key_size);

			// Write the new position (index) and the sort key
			uint128_t to_write = (uint128_t)sort_key << sort_key_shift;
			to_write |= (uint128_t)index << index_shift;

			uint8_t bytes[16];
			IntTo16Bytes(bytes, to_write);
			L_sort_manager->AddToCache(bytes);
			added_to_cache++;

			// Every EPP entries, writes a park
			if (index % kEntriesPerPark == 0) {
				if (index != 0) {
					WriteParkToFile(
						context->tmCache,
						tmp2_buffered_disk,
						final_table_begin_pointers[table_index],
						park_index,
						park_size_bytes,
						checkpoint_line_point,
						park_deltas,
						park_stubs,
						k,
						table_index,
						park_buffer.get(),
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
				context->tmCache,
				tmp2_buffered_disk,
				final_table_begin_pointers[table_index],
				park_index,
				park_size_bytes,
				checkpoint_line_point,
				park_deltas,
				park_stubs,
				k,
				table_index,
				park_buffer.get(),
				park_buffer_size);
			final_entries_written += (park_stubs.size() + 1);
		}

		Encoding::ANSFree(kRValues[table_index - 1]);
		std::cout << "\tWrote " << final_entries_written << " entries" << std::endl;

		final_table_begin_pointers[table_index + 1] =
			final_table_begin_pointers[table_index] + (park_index + 1) * park_size_bytes;

		final_table_writer = header_size - 8 * (10 - table_index);
		IntToEightBytes(table_pointer_bytes, final_table_begin_pointers[table_index + 1]);
		tmp2_disk.Write(final_table_writer, (table_pointer_bytes), 8);
		final_table_writer += 8;

		table_timer.PrintElapsed("Total compress table time:");

		left_disk.FreeMemory();
		right_disk.FreeMemory();
		//if (flags & SHOW_PROGRESS) {
		//	progress(3, table_index, 6);
		//}
	}

	L_sort_manager->FreeMemory();
	park_buffer.reset();
	tmp2_buffered_disk.FreeMemory();

	// These results will be used to write table P7 and the checkpoint tables in phase 4.
	return Phase3Results{
		final_table_begin_pointers,
		final_entries_written,
		new_pos_entry_size_bytes * 8,
		header_size,
		std::move(L_sort_manager)};
}

void RunPhase4(
	DiskPlotterContext* context,
	uint8_t k,
	uint8_t pos_size,
	FileDisk& tmp2_disk,
	Phase3Results& res,
	const uint8_t flags,
	const int max_phase4_progress_updates)
{
	uint32_t P7_park_size = ByteAlign((k + 1) * kEntriesPerPark) / 8;
	uint64_t number_of_p7_parks =
		((res.final_entries_written == 0 ? 0 : res.final_entries_written - 1) / kEntriesPerPark) +
		1;

	uint64_t begin_byte_C1 = res.final_table_begin_pointers[7] + number_of_p7_parks * P7_park_size;

	uint64_t total_C1_entries = cdiv(res.final_entries_written, kCheckpoint1Interval);
	uint64_t begin_byte_C2 = begin_byte_C1 + (total_C1_entries + 1) * (ByteAlign(k) / 8);
	uint64_t total_C2_entries = cdiv(total_C1_entries, kCheckpoint2Interval);
	uint64_t begin_byte_C3 = begin_byte_C2 + (total_C2_entries + 1) * (ByteAlign(k) / 8);

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

	uint8_t* right_entry_buf;
	auto C1_entry_buf = new uint8_t[ByteAlign(k) / 8];
	auto C3_entry_buf = new uint8_t[size_C3];
	auto P7_entry_buf = new uint8_t[P7_park_size];

	std::cout << "\tStarting to write C1 and C3 tables" << std::endl;

	ParkBits to_write_p7;
	const int progress_update_increment = res.final_entries_written / max_phase4_progress_updates;

	// We read each table7 entry, which is sorted by f7, but we don't need f7 anymore. Instead,
	// we will just store pos6, and the deltas in table C3, and checkpoints in tables C1 and C2.
	for (uint64_t f7_position = 0; f7_position < res.final_entries_written; f7_position++) {
		right_entry_buf = res.table7_sm->ReadEntry(plot_file_reader);

		plot_file_reader += right_entry_size_bytes;
		uint64_t entry_y = SliceInt64FromBytes(right_entry_buf, 0, k);
		uint64_t entry_new_pos = SliceInt64FromBytes(right_entry_buf, k, pos_size);

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
			tmp2_disk.Write(final_file_writer_1, (C1_entry_buf), ByteAlign(k) / 8);
			final_file_writer_1 += ByteAlign(k) / 8;
			if (num_C1_entries > 0) {
				final_file_writer_2 = begin_byte_C3 + (num_C1_entries - 1) * size_C3;
				size_t num_bytes =
					Encoding::ANSEncodeDeltas(context->tmCache, deltas_to_write, kC3R, C3_entry_buf + 2) + 2;

				// We need to be careful because deltas are variable sized, and they need to fit
				assert(size_C3 * 8 > num_bytes);

				// Write the size
				IntToTwoBytes(C3_entry_buf, num_bytes - 2);

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
			deltas_to_write.push_back(entry_y - prev_y);
			prev_y = entry_y;
		}
		//if (flags & SHOW_PROGRESS && f7_position % progress_update_increment == 0) {
		//	progress(4, f7_position, res.final_entries_written);
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
		size_t num_bytes = Encoding::ANSEncodeDeltas(context->tmCache,deltas_to_write, kC3R, C3_entry_buf + 2);
		memset(C3_entry_buf + num_bytes + 2, 0, size_C3 - (num_bytes + 2));
		final_file_writer_2 = begin_byte_C3 + (num_C1_entries - 1) * size_C3;

		// Write the size
		IntToTwoBytes(C3_entry_buf, num_bytes);

		tmp2_disk.Write(final_file_writer_2, (C3_entry_buf), size_C3);
		final_file_writer_2 += size_C3;
		Encoding::ANSFree(kC3R);
	}

	Bits(0, ByteAlign(k)).ToBytes(C1_entry_buf);
	tmp2_disk.Write(final_file_writer_1, (C1_entry_buf), ByteAlign(k) / 8);
	final_file_writer_1 += ByteAlign(k) / 8;
	std::cout << "\tFinished writing C1 and C3 tables" << std::endl;
	std::cout << "\tWriting C2 table" << std::endl;

	for (Bits& C2_entry : C2) {
		C2_entry.ToBytes(C1_entry_buf);
		tmp2_disk.Write(final_file_writer_1, (C1_entry_buf), ByteAlign(k) / 8);
		final_file_writer_1 += ByteAlign(k) / 8;
	}
	Bits(0, ByteAlign(k)).ToBytes(C1_entry_buf);
	tmp2_disk.Write(final_file_writer_1, (C1_entry_buf), ByteAlign(k) / 8);
	final_file_writer_1 += ByteAlign(k) / 8;
	std::cout << "\tFinished writing C2 table" << std::endl;

	delete[] C3_entry_buf;
	delete[] C1_entry_buf;
	delete[] P7_entry_buf;

	final_file_writer_1 = res.header_size - 8 * 3;
	uint8_t table_pointer_bytes[8];

	// Writes the pointers to the start of the tables, for proving
	for (int i = 8; i <= 10; i++) {
		IntToEightBytes(table_pointer_bytes, res.final_table_begin_pointers[i]);
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
