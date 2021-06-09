#include "sort_manager.hpp"

void QuickSort::SortInner(
    uint8_t *memory,
    uint64_t memory_len,
    uint32_t L,
    uint32_t bits_begin,
    uint64_t begin,
    uint64_t end,
    uint8_t *pivot_space)
{
    if (end - begin <= 32) {
        for (uint64_t i = begin + 1; i < end; i++) {
            uint64_t j = i;
            memcpy(pivot_space, memory + i * L, L);
            while (j > begin &&
                   Util::MemCmpBits(memory + (j - 1) * L, pivot_space, L, bits_begin) > 0) {
                memcpy(memory + j * L, memory + (j - 1) * L, L);
                j--;
            }
            memcpy(memory + j * L, pivot_space, L);
        }
        return;
    }

    uint64_t lo = begin;
    uint64_t hi = end - 1;

    memcpy(pivot_space, memory + (hi * L), L);
    bool left_side = true;

    while (lo < hi) {
        if (left_side) {
            if (Util::MemCmpBits(memory + lo * L, pivot_space, L, bits_begin) < 0) {
                ++lo;
            } else {
                memcpy(memory + hi * L, memory + lo * L, L);
                --hi;
                left_side = false;
            }
        } else {
            if (Util::MemCmpBits(memory + hi * L, pivot_space, L, bits_begin) > 0) {
                --hi;
            } else {
                memcpy(memory + lo * L, memory + hi * L, L);
                ++lo;
                left_side = true;
            }
        }
    }
    memcpy(memory + lo * L, pivot_space, L);
    if (lo - begin <= end - lo) {
        SortInner(memory, memory_len, L, bits_begin, begin, lo, pivot_space);
        SortInner(memory, memory_len, L, bits_begin, lo + 1, end, pivot_space);
    } else {
        SortInner(memory, memory_len, L, bits_begin, lo + 1, end, pivot_space);
        SortInner(memory, memory_len, L, bits_begin, begin, lo, pivot_space);
    }
}

void QuickSort::Sort(
    uint8_t *const memory,
    uint32_t const entry_len,
    uint64_t const num_entries,
    uint32_t const bits_begin)
{
    uint64_t const memory_len = (uint64_t)entry_len * num_entries;
    auto const pivot_space = std::make_unique<uint8_t[]>(entry_len);
    SortInner(memory, memory_len, entry_len, bits_begin, 0, num_entries, pivot_space.get());
}

bool UniformSort::IsPositionEmpty(const uint8_t *memory, uint32_t const entry_len)
{
    // no entry can be larger than 256 bytes (the blake3 output size)
    const static uint8_t zeros[256] = {};
    return memcmp(memory, zeros, entry_len) == 0;
}

void UniformSort::SortToMemory(
	thread_pool& pool,
    FileDisk &input_disk,
    uint64_t const input_disk_begin,
    uint8_t *const memory,
    uint32_t const entry_len,
    uint64_t const num_entries,
    uint32_t const bits_begin)
{
    uint64_t const memory_len = Util::RoundSize(num_entries) * entry_len;
    auto const swap_space = std::make_unique<uint8_t[]>(entry_len);
    auto buffer = std::make_unique<uint8_t[]>(BUF_SIZE);
    auto next_buffer = std::make_unique<uint8_t[]>(BUF_SIZE);
    uint64_t bucket_length = 0;

    uint64_t buf_size = 0;
    uint64_t next_buf_size = 0;
    uint64_t buf_ptr = 0;
    uint64_t swaps = 0;
    uint64_t read_pos = input_disk_begin;
    const uint64_t full_buffer_size = (uint64_t)BUF_SIZE / entry_len;

    std::future<bool> next_read_job;

    auto read_next = [&pool,
					  &input_disk,
                      full_buffer_size,
                      num_entries,
                      entry_len,
                      &read_pos,
                      input_disk_begin,
                      &next_buffer,
                      &next_buf_size,
                      &next_read_job]() {
        next_buf_size =
            std::min(full_buffer_size, num_entries - ((read_pos - input_disk_begin) / entry_len));
        const uint64_t read_size = next_buf_size * entry_len;
        if (read_size == 0) {
            return;
        }
        next_read_job = pool.submit([&input_disk, read_pos, read_size, buffer = next_buffer.get()] {
            input_disk.Read(read_pos, buffer, read_size);
        });
        read_pos += next_buf_size * entry_len;
    };

    // Start reading the first block. Memset will take a while.
    read_next();

    // The number of buckets needed (the smallest power of 2 greater than 2 * num_entries).
    while ((1ULL << bucket_length) < 2 * num_entries) bucket_length++;
    memset(memory, 0, memory_len);

    for (uint64_t i = 0; i < num_entries; i++) {
        if (buf_size == 0) {
            // If read buffer is empty, wait for the reader thread, get the
            // buffer and immediately start reading the next block.
            next_read_job.wait();
            buf_size = next_buf_size;
            buffer.swap(next_buffer);
            buf_ptr = 0;
            read_next();
        }
        buf_size--;
        // First unique bits in the entry give the expected position of it in the sorted array.
        // We take 'bucket_length' bits starting with the first unique one.
        uint64_t pos =
            Util::ExtractNum(buffer.get() + buf_ptr, entry_len, bits_begin, (uint32_t)bucket_length) *
            entry_len;
        // As long as position is occupied by a previous entry...
        while (!IsPositionEmpty(memory + pos, entry_len) && pos < memory_len) {
            // ...store there the minimum between the two and continue to push the higher one.
            if (Util::MemCmpBits(memory + pos, buffer.get() + buf_ptr, entry_len, bits_begin) > 0) {
                memcpy(swap_space.get(), memory + pos, entry_len);
                memcpy(memory + pos, buffer.get() + buf_ptr, entry_len);
                memcpy(buffer.get() + buf_ptr, swap_space.get(), entry_len);
                swaps++;
            }
            pos += entry_len;
        }
        // Push the entry in the first free spot.
        memcpy(memory + pos, buffer.get() + buf_ptr, entry_len);
        buf_ptr += entry_len;
    }
    uint64_t entries_written = 0;
    // Search the memory buffer for occupied entries.
    for (uint64_t pos = 0; entries_written < num_entries && pos < memory_len; pos += entry_len) {
        if (!IsPositionEmpty(memory + pos, entry_len)) {
            // We've found an entry.
            // write the stored entry itself.
            memcpy(memory + entries_written * entry_len, memory + pos, entry_len);
            entries_written++;
        }
    }

    assert(entries_written == num_entries);
}

SortManager::SortManager(
	DiskPlotterContext& context,
    uint64_t const memory_size,
    uint32_t const num_buckets,
    uint32_t const log_num_buckets,
    uint16_t const entry_size,
    const std::wstring &tmp_dirname,
    const std::wstring &filename,
    uint32_t begin_bits,
    uint64_t const stripe_size,
    strategy_t const sort_strategy /*= strategy_t::uniform*/,
    uint64_t max_memory_size /*= 0*/)
    : memory_size_(memory_size),
      entry_size_(entry_size),
      begin_bits_(begin_bits),
      log_num_buckets_(log_num_buckets),
      prev_bucket_buf_size(2 * (stripe_size + 10 * (kBC / pow(2, kExtraBits))) * entry_size)
      // 7 bytes head-room for SliceInt64FromBytes()
      ,
      entry_buf_(new uint8_t[entry_size + (uint16_t)7]),
      strategy_(sort_strategy),
      max_memory_size_(max_memory_size),
	  context(context)
{
    // Cross platform way to concatenate paths, gulrak library.
    std::vector<fs::path> bucket_filenames = std::vector<fs::path>();

    buckets_.reserve(num_buckets);
    for (size_t bucket_i = 0; bucket_i < num_buckets; bucket_i++) {
        std::ostringstream bucket_number_padded;
        bucket_number_padded << std::internal << std::setfill('0') << std::setw(3) << bucket_i;

        fs::path const bucket_filename =
            fs::path(tmp_dirname) /
            fs::path(filename + L".sort_bucket_" + s2ws(bucket_number_padded.str()) + L".tmp");
        fs::remove(bucket_filename);

        buckets_.emplace_back(FileDisk(bucket_filename));
    }
}

void SortManager::AddToCache(const Bits &entry)
{
    entry.ToBytes(entry_buf_.get());
    return AddToCache(entry_buf_.get());
}

void SortManager::AddToCache(const uint8_t *entry)
{
    if (unlikely(this->done)) {
        throw InvalidValueException("Already finished.");
    }
    uint64_t const bucket_index =
        Util::ExtractNum(entry, entry_size_, begin_bits_, log_num_buckets_);
    bucket_t &b = buckets_[bucket_index];
    b.file.Write(b.write_pointer, entry, entry_size_);
    b.write_pointer += entry_size_;
}

uint8_t const *SortManager::Read(uint64_t begin, uint64_t length)
{
    assert(length <= entry_size_);
    return ReadEntry(begin);
}

void SortManager::Write(uint64_t, uint8_t const *, uint64_t)
{
    assert(false);
    throw InvalidStateException("Invalid Write() called on SortManager");
}

void SortManager::Truncate(uint64_t new_size)
{
    if (unlikely(new_size != 0)) {
        assert(false);
        throw InvalidStateException("Invalid Truncate() called on SortManager");
    }

    FlushCache();
    FreeMemory();
}

std::string SortManager::GetFileName() { return "<SortManager>"; }

void SortManager::FreeMemory()
{
    for (auto &b : buckets_) {
        b.file.FreeMemory();
        // the underlying file will be re-opened again on-demand
        b.underlying_file.Close();
    }
    prev_bucket_buf_.reset();
    memory_start_.reset();
    next_memory_start_.reset();
    final_position_end = 0;
    // TODO: Ideally, bucket files should be deleted as we read them (in the
    // last reading pass over them)
}

uint8_t *SortManager::ReadEntry(uint64_t position)
{
    if (unlikely(position < this->final_position_start)) {
        if (!(position >= this->prev_bucket_position_start)) {
            throw InvalidStateException("Invalid prev bucket start");
        }
        // this is allocated lazily, make sure it's here
        assert(prev_bucket_buf_);
        return (prev_bucket_buf_.get() + (position - this->prev_bucket_position_start));
    }

    if (!this->done) {
        this->done = true;
        if (!memory_start_) {
            // we allocate the memory to sort the bucket in lazily. It'se freed
            // in FreeMemory() or the destructor
            memory_start_.reset(new uint8_t[memory_size_]);
        }
        if (max_memory_size_ == 2 * memory_size_) {
            next_memory_start_.reset(new uint8_t[memory_size_]);
            uint64_t bucket_i = next_bucket_to_sort++;
            next_sort_job = context.pool.submit([bucket_i, memory_start = next_memory_start_.get(), this] {
                SortBucket(bucket_i, memory_start, memory_size_);
            });
        }
    }

    while (position >= this->final_position_end) {
        WaitForSortedBucket(context.pool);
    }

    if (unlikely(!(this->final_position_end > position))) {
        throw InvalidValueException("Position too large");
    }
    if (unlikely(!(this->final_position_start <= position))) {
        throw InvalidValueException("Position too small");
    }
    return memory_start_.get() + (position - this->final_position_start);
}

bool SortManager::CloseToNewBucket(uint64_t position) const
{
    if (!(position <= this->final_position_end)) {
        return this->next_bucket_to_sort < buckets_.size();
    };
    return (
        position + prev_bucket_buf_size / 2 >= this->final_position_end &&
        this->next_bucket_to_sort < buckets_.size());
}

void SortManager::TriggerNewBucket(uint64_t position)
{
    if (unlikely(!(position <= this->final_position_end))) {
        throw InvalidValueException("Triggering bucket too late");
    }
    if (unlikely(!(position >= this->final_position_start))) {
        throw InvalidValueException("Triggering bucket too early");
    }

    if (memory_start_) {
        // save some of the current bucket, to allow some reverse-tracking
        // in the reading pattern,
        // position is the first position that we need in the new array
        uint64_t const cache_size = (this->final_position_end - position);
        prev_bucket_buf_.reset(new uint8_t[prev_bucket_buf_size]);
        memset(prev_bucket_buf_.get(), 0x00, this->prev_bucket_buf_size);
        memcpy(
            prev_bucket_buf_.get(),
            memory_start_.get() + position - this->final_position_start,
            cache_size);
    }

    if (!memory_start_) {
        // we allocate the memory to sort the bucket in lazily. It'se freed
        // in FreeMemory() or the destructor
        memory_start_.reset(new uint8_t[memory_size_]);
    }

    uint64_t bucket_i = next_bucket_to_sort++;
    SortBucket(bucket_i, memory_start_.get(), memory_size_);
    this->final_position_start = this->final_position_end;
    this->final_position_end += buckets_[bucket_i].write_pointer;
    this->prev_bucket_position_start = position;
}

void SortManager::FlushCache()
{
    for (auto &b : buckets_) {
        b.file.FlushCache();
    }
    final_position_end = 0;
    memory_start_.reset();
    next_memory_start_.reset();
}

SortManager::~SortManager()
{
    // Close and delete files in case we exit without doing the sort
    for (auto &b : buckets_) {
        std::string const filename = b.file.GetFileName();
        b.underlying_file.Close();
        fs::remove(fs::path(filename));
    }
}

void SortManager::WaitForSortedBucket(thread_pool& pool)
{
    uint64_t bucket_i = next_bucket_to_sort++;
    if (!next_memory_start_) {
        // basic case if we cannot parallelize
        SortBucket(bucket_i, memory_start_.get(), memory_size_);
        this->final_position_start = this->final_position_end;
        this->final_position_end += buckets_[bucket_i].write_pointer;
        return;
    }
    // when we're waiting for a sorted bucket, we have memory_start_ buffer
    // empty. So we start a new job with that buffer.
    auto new_job = pool.submit([bucket_i, memory_start = memory_start_.get(), this] {
        SortBucket(bucket_i, memory_start, memory_size_);
    });
    next_sort_job.wait();

    // the previous buffer finished. Let memory_start_ point to its buffer.
    // next_memory_start_ is now used by the job we just started.
    memory_start_.swap(next_memory_start_);
    this->final_position_start = this->final_position_end;
    this->final_position_end += buckets_[bucket_i - 1].write_pointer;
    next_sort_job = std::move(new_job);
}

void SortManager::SortBucket(
    const uint64_t bucket_i,
    uint8_t *const memory_start,
    const uint64_t memory_size)
{
    bucket_t &b = buckets_[bucket_i];
    uint64_t const bucket_entries = b.write_pointer / entry_size_;
    uint64_t const entries_fit_in_memory = memory_size / entry_size_;

    double const have_ram = entry_size_ * entries_fit_in_memory / (1024.0 * 1024.0 * 1024.0);
    double const qs_ram = entry_size_ * bucket_entries / (1024.0 * 1024.0 * 1024.0);
    double const u_ram = Util::RoundSize(bucket_entries) * entry_size_ / (1024.0 * 1024.0 * 1024.0);

    if (bucket_entries > entries_fit_in_memory) {
        throw InsufficientMemoryException(
            "Not enough memory for sort in memory. Need to sort " +
            std::to_string(b.write_pointer / (1024.0 * 1024.0 * 1024.0)) + "GiB");
    }
    bool const last_bucket =
        (bucket_i == buckets_.size() - 1) || buckets_[bucket_i + 1].write_pointer == 0;

    bool const force_quicksort = (strategy_ == strategy_t::quicksort) ||
                                 (strategy_ == strategy_t::quicksort_last && last_bucket);

    // Do SortInMemory algorithm if it fits in the memory
    // (number of entries required * entry_size_) <= total memory available
    if (!force_quicksort && Util::RoundSize(bucket_entries) * entry_size_ <= memory_size_) {
        context.sync_out.println(
            "\tBucket ",
            bucket_i,
            " uniform sort. Ram: ",
            std::fixed,
            std::setprecision(3),
            have_ram,
            "GiB, u_sort min: ",
            u_ram,
            "GiB, qs min: ",
            qs_ram,
            "GiB.");
        UniformSort::SortToMemory(
			context.pool,
            b.underlying_file,
            0,
            memory_start,
            entry_size_,
            bucket_entries,
            begin_bits_ + log_num_buckets_);
    } else {
        // Are we in Compress phrase 1 (quicksort=1) or is it the last bucket (quicksort=2)?
        // Perform quicksort if so (SortInMemory algorithm won't always perform well), or if we
        // don't have enough memory for uniform sort
        context.sync_out.println(
            "\tBucket ",
            bucket_i,
            " QS. Ram: ",
            std::fixed,
            std::setprecision(3),
            have_ram,
            "GiB, u_sort min: ",
            u_ram,
            "GiB, qs min: ",
            qs_ram,
            "GiB. force_qs: ",
            force_quicksort);
        b.underlying_file.Read(0, memory_start, bucket_entries * entry_size_);
        QuickSort::Sort(memory_start, entry_size_, bucket_entries, begin_bits_ + log_num_buckets_);
    }

    // Deletes the bucket file
    std::string filename = b.file.GetFileName();
    b.underlying_file.Close();
    fs::remove(fs::path(filename));
}
