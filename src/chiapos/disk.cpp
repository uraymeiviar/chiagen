#include "disk.hpp"
#include "stdiox.hpp"

#if ENABLE_LOGGING
// logging is currently unix / bsd only: use <fstream> or update
// calls to ::open and ::write to port to windows
#include <fcntl.h>
#include <unistd.h>
#include <mutex>
#include <unordered_map>
#include <cinttypes>

enum class op_t : int { read, write};

void disk_log(fs::path const& filename, op_t const op, uint64_t offset, uint64_t length)
{
    static std::mutex m;
    static std::unordered_map<std::string, int> file_index;
    static auto const start_time = std::chrono::steady_clock::now();
    static int next_file = 0;

    auto const timestamp = std::chrono::steady_clock::now() - start_time;

    int fd = ::open("disk.log", O_WRONLY | O_CREAT | O_APPEND, 0755);

    std::unique_lock<std::mutex> l(m);

    char buffer[512];

    int const index = [&] {
        auto it = file_index.find(filename.string());
        if (it != file_index.end()) return it->second;
        file_index[filename.string()] = next_file;

        int const len = std::snprintf(buffer, sizeof(buffer)
            , "# %d %s\n", next_file, filename.string().c_str());
        ::write(fd, buffer, len);
        return next_file++;
    }();

    // timestamp (ms), start-offset, end-offset, operation (0 = read, 1 = write), file_index
    int const len = std::snprintf(buffer, sizeof(buffer)
        , "%" PRId64 "\t%" PRIu64 "\t%" PRIu64 "\t%d\t%d\n"
        , std::chrono::duration_cast<std::chrono::milliseconds>(timestamp).count()
        , offset
        , offset + length
        , int(op)
        , index);
    ::write(fd, buffer, len);
    ::close(fd);
}
#endif

FileDisk::FileDisk(const fs::path& filename)
{
    filename_ = filename;
    Open(writeFlag);
}

 FileDisk::FileDisk(FileDisk&& fd)
{
    filename_ = std::move(fd.filename_);
    f_ = fd.f_;
    fd.f_ = nullptr;
}

void FileDisk::Close()
{
    if (f_ == nullptr)
        return;
    ::fclose(f_);
    f_ = nullptr;
    readPos = 0;
    writePos = 0;
}

void FileDisk::Read(uint64_t begin, uint8_t* memcache, uint64_t length)
{
    Open(retryOpenFlag);
#if ENABLE_LOGGING
    disk_log(filename_, op_t::read, begin, length);
#endif
    // Seek, read, and replace into memcache
    uint64_t amtread;
    do {
        if ((!bReading) || (begin != readPos)) {
			FSEEK(f_, begin, SEEK_SET);
            bReading = true;
        }
        amtread = fread(reinterpret_cast<char*>(memcache), sizeof(uint8_t), length, f_);
        readPos = begin + amtread;
        if (amtread != length) {
            std::cout << "Only read " << amtread << " of " << length << " bytes at offset " << begin
                      << " from " << filename_ << " with length " << writeMax << ". Error "
                      << ferror(f_) << ". Retrying in five minutes." << std::endl;
            // Close, Reopen, and re-seek the file to recover in case the filesystem
            // has been remounted.
            Close();
            bReading = false;
            std::this_thread::sleep_for(5min);
            Open(retryOpenFlag);
        }
    } while (amtread != length);
}

void FileDisk::Write(uint64_t begin, const uint8_t* memcache, uint64_t length)
{
    Open(writeFlag | retryOpenFlag);
#if ENABLE_LOGGING
    disk_log(filename_, op_t::write, begin, length);
#endif
    // Seek and write from memcache
    uint64_t amtwritten;
    do {
        if ((bReading) || (begin != writePos)) {
			FSEEK(f_, begin, SEEK_SET);
            bReading = false;
        }
        amtwritten = ::fwrite(reinterpret_cast<const char*>(memcache), sizeof(uint8_t), length, f_);
        writePos = begin + amtwritten;
        if (writePos > writeMax)
            writeMax = writePos;
        if (amtwritten != length) {
            // If an error occurs, the resulting value of the file-position indicator for the stream
            // is unspecified. https://pubs.opengroup.org/onlinepubs/007904975/functions/fwrite.html
            //
            // And in the code above if error occurs with 0 bytes written (full disk) it will not
            // reset the pointer (writePos will still be equal to begin), however it need to be
            // reseted.
            //
            // Otherwise this causes #234 - in phase3, when this bucket is read, it goes into
            // endless loop.
            //
            // Thanks tinodj!
            writePos = UINT64_MAX;
            std::cout << "Only wrote " << amtwritten << " of " << length << " bytes at offset "
                      << begin << " to " << filename_ << " with length " << writeMax << ". Error "
                      << ferror(f_) << ". Retrying in five minutes." << std::endl;
            // Close, Reopen, and re-seek the file to recover in case the filesystem
            // has been remounted.
            Close();
            bReading = false;
            std::this_thread::sleep_for(5min);
            Open(writeFlag | retryOpenFlag);
        }
    } while (amtwritten != length);
}

void FileDisk::Truncate(uint64_t new_size)
{
    Close();
    fs::resize_file(filename_, new_size);
}

void FileDisk::Open(uint8_t flags /*= 0*/)
{
    // if the file is already open, don't do anything
    if (f_)
        return;

    // Opens the file for reading and writing
    do {
        f_ = FOPEN(filename_.wstring().c_str(), (flags & writeFlag) ? L"w+b" : L"r+b");
        if (f_ == nullptr) {
            char err_buffer[256];
            if (::strerror_s(err_buffer, 255, errno) != 0) {
                strncpy_s(err_buffer, "unknown error", strlen("unknown error"));
            }
            err_buffer[255] = '\0';
            std::string error_message =
                "Could not open " + filename_.string() + ": " + std::string(err_buffer) + ".";
            if (flags & retryOpenFlag) {
                std::cout << error_message << " Retrying in five minutes." << std::endl;
                std::this_thread::sleep_for(5min);
            } else {
                throw InvalidValueException(error_message);
            }
        }
    } while (f_ == nullptr);
}

BufferedDisk::BufferedDisk(FileDisk* disk, uint64_t file_size) : disk_(disk), file_size_(file_size)
{
}

uint8_t const* BufferedDisk::Read(uint64_t begin, uint64_t length)
{
    assert(length < read_ahead);
    NeedReadCache();
    // all allocations need 7 bytes head-room, since
    // SliceInt64FromBytes() may overrun by 7 bytes
    if (read_buffer_start_ <= begin && read_buffer_start_ + read_buffer_size_ >= begin + length &&
        read_buffer_start_ + read_ahead >= begin + length + 7) {
        // if the read is entirely inside the buffer, just return it
        return read_buffer_.get() + (begin - read_buffer_start_);
    } else if (
        begin >= read_buffer_start_ || begin == 0 || read_buffer_start_ == std::uint64_t(-1)) {
        // if the read is beyond the current buffer (i.e.
        // forward-sequential) move the buffer forward and read the next
        // buffer-capacity number of bytes.
        // this is also the case we enter the first time we perform a read,
        // where we haven't read anything into the buffer yet. Note that
        // begin == 0 won't reliably detect that case, sinec we may have
        // discarded the first entry and start at some low offset but still
        // greater than 0
        read_buffer_start_ = begin;
        uint64_t const amount_to_read = std::min(file_size_ - read_buffer_start_, read_ahead);
        disk_->Read(begin, read_buffer_.get(), amount_to_read);
        read_buffer_size_ = amount_to_read;
        return read_buffer_.get();
    } else {
        // ideally this won't happen
        std::cout << "Disk read position regressed. It's optimized for forward scans. Performance "
                     "may suffer\n"
                  << "   read-offset: " << begin << " read-length: " << length
                  << " file-size: " << file_size_ << " read-buffer: [" << read_buffer_start_ << ", "
                  << read_buffer_size_ << "]"
                  << " file: " << disk_->GetFileName() << '\n';
        static uint8_t temp[128];
        // all allocations need 7 bytes head-room, since
        // SliceInt64FromBytes() may overrun by 7 bytes
        assert(length <= sizeof(temp) - 7);

        // if we're going backwards, don't wipe out the cache. We assume
        // forward sequential access
        disk_->Read(begin, temp, length);
        return temp;
    }
}

void BufferedDisk::Write(uint64_t const begin, const uint8_t* memcache, uint64_t const length)
{
    NeedWriteCache();
    if (begin == write_buffer_start_ + write_buffer_size_) {
        if (write_buffer_size_ + length <= write_cache) {
            ::memcpy(write_buffer_.get() + write_buffer_size_, memcache, length);
            write_buffer_size_ += length;
            return;
        }
        FlushCache();
    }

    if (write_buffer_size_ == 0 && write_cache >= length) {
        write_buffer_start_ = begin;
        ::memcpy(write_buffer_.get() + write_buffer_size_, memcache, length);
        write_buffer_size_ = length;
        return;
    }

    disk_->Write(begin, memcache, length);

    // if a write was requested to an unexpected location, also flush the
    // cache
    FlushCache();
}

void BufferedDisk::Truncate(uint64_t const new_size)
{
    FlushCache();
    disk_->Truncate(new_size);
    file_size_ = new_size;
    FreeMemory();
}

std::string BufferedDisk::GetFileName() { return disk_->GetFileName(); }

void BufferedDisk::FreeMemory()
{
    FlushCache();

    read_buffer_.reset();
    write_buffer_.reset();
    read_buffer_size_ = 0;
    write_buffer_size_ = 0;
}

void BufferedDisk::FlushCache()
{
    if (write_buffer_size_ == 0)
        return;

    disk_->Write(write_buffer_start_, write_buffer_.get(), write_buffer_size_);
    write_buffer_size_ = 0;
}

void BufferedDisk::NeedReadCache()
{
    if (read_buffer_)
        return;
    read_buffer_.reset(new uint8_t[read_ahead]);
    read_buffer_start_ = -1;
    read_buffer_size_ = 0;
}

void BufferedDisk::NeedWriteCache()
{
    if (write_buffer_)
        return;
    write_buffer_.reset(new uint8_t[write_cache]);
    write_buffer_start_ = -1;
    write_buffer_size_ = 0;
}

FilteredDisk::FilteredDisk(BufferedDisk underlying, bitfield filter, int entry_size)
    : filter_(std::move(filter)), underlying_(std::move(underlying)), entry_size_(entry_size)
{
    assert(entry_size_ > 0);
    while (!filter_.get(last_idx_)) {
        last_physical_ += entry_size_;
        ++last_idx_;
    }
    assert(filter_.get(last_idx_));
    assert(last_physical_ == last_idx_ * entry_size_);
}

uint8_t const* FilteredDisk::Read(uint64_t begin, uint64_t length)
{
    // we only support a single read-pass with no going backwards
    assert(begin >= last_logical_);
    assert((begin % entry_size_) == 0);
    assert(filter_.get(last_idx_));
    assert(last_physical_ == last_idx_ * entry_size_);

    if (begin > last_logical_) {
        // last_idx_ et.al. always points to an entry we have (i.e. the bit
        // is set). So when we advance from there, we always take at least
        // one step on all counters.
        last_logical_ += entry_size_;
        last_physical_ += entry_size_;
        ++last_idx_;

        while (begin > last_logical_) {
            if (filter_.get(last_idx_)) {
                last_logical_ += entry_size_;
            }
            last_physical_ += entry_size_;
            ++last_idx_;
        }

        while (!filter_.get(last_idx_)) {
            last_physical_ += entry_size_;
            ++last_idx_;
        }
    }

    assert(filter_.get(last_idx_));
    assert(last_physical_ == last_idx_ * entry_size_);
    assert(begin == last_logical_);
    return underlying_.Read(last_physical_, length);
}

void FilteredDisk::Write(uint64_t begin, const uint8_t* memcache, uint64_t length)
{
    assert(false);
    throw std::runtime_error("Write() called on read-only disk abstraction");
}

void FilteredDisk::Truncate(uint64_t new_size)
{
    underlying_.Truncate(new_size);
    if (new_size == 0)
        filter_.free_memory();
}

void FilteredDisk::FreeMemory()
{
    filter_.free_memory();
    underlying_.FreeMemory();
}
