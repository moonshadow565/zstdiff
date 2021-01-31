#include "mmap.hpp"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif

auto MMapError::with_header(char const* header) noexcept -> MMapError {
#ifdef _WIN32
    return { header, static_cast<int>(GetLastError()) };
#else
    return { header, static_cast<int>(errno) };
#endif
}

auto MMapRaw::close_raw(std::optional<std::size_t> trunc_size) noexcept -> MMapError {
#ifdef _WIN32
    if (this->file_handle_ != 0) {
        if (this->map_handle_ != 0) {
            if (this->map_data_ != nullptr) {
                if (trunc_size && *trunc_size < this->file_size_) {
                    ::FlushViewOfFile(this->map_data_, *trunc_size);
                } else {
                    ::FlushViewOfFile(this->map_data_, this->file_size_);
                }
                if (::UnmapViewOfFile(this->map_data_) == FALSE) {
                    return MMapError::with_header("Unmap file view");
                }
                this->map_data_ = nullptr;
            }

            auto const raw_map_handle = reinterpret_cast<HANDLE>(this->map_handle_);
            if (::CloseHandle(raw_map_handle) == FALSE) {
                return MMapError::with_header("Close map handle");
            }
            this->map_handle_ = 0;
        }

        auto const raw_file_handle = reinterpret_cast<HANDLE>(this->file_handle_);
        if (trunc_size && this->file_size_ != *trunc_size) {
            auto const new_file_size = LARGE_INTEGER { .QuadPart = static_cast<LONGLONG>(*trunc_size) };
            if (::SetFilePointerEx(raw_file_handle, new_file_size, nullptr, FILE_BEGIN) == FALSE) {
                return MMapError::with_header("Trunc set size");
            }
            if (::SetEndOfFile(raw_file_handle) == FALSE) {
                return MMapError::with_header("Trunc set end");
            }
            if (::SetFilePointerEx(raw_file_handle, { .QuadPart = 0 }, nullptr, FILE_BEGIN) == FALSE) {
                return MMapError::with_header("Trunc set beg");
            }
            this->file_size_ = *trunc_size;
        }
        ::FlushFileBuffers(raw_file_handle);
        if (::CloseHandle(raw_file_handle) == FALSE) {
            return MMapError::with_header("Close file handle");
        }
        this->file_handle_ = 0;
        this->file_size_ = 0;
    }
#else

    if (this->file_handle_ != 0) {
        if (this->map_data_ != nullptr) {
            if (trunc_size && *trunc_size < this->file_size_) {
                ::msync(this->map_data_, *trunc_size, MS_SYNC);
            } else {
                ::msync(this->map_data_, this->file_size_, MS_SYNC);
            }
            if (::munmap(this->map_data_, this->file_size_) != 0) {
                return MMapError::with_header("unmap file view");
            }
            this->map_data_ = nullptr;
        }

        auto const raw_file_handle = static_cast<int>(this->file_handle_);
        if (trunc_size && this->file_size_ != *trunc_size) {
            if (::ftruncate(raw_file_handle, static_cast<off_t>(*trunc_size)) != 0) {
                return MMapError::with_header("trunc set size");
            }
            this->file_size_ = *trunc_size;
        }
        if (::close(raw_file_handle) != 0) {
            return MMapError::with_header("close file handle");
        }
        this->file_handle_ = 0;
        this->file_size_ = 0;
    }
#endif
    return {};
}

auto MMapRaw::close_or_panic() noexcept -> void {
    if (auto error = this->close_raw()) {
        ::fprintf(stderr, "Failed to close at %s because %d(%s)\n",
                  error.header, error.errnum, ::strerror(error.errnum));
        ::exit(EXIT_FAILURE);
    }
}

auto MMapRaw::close_on_error(char const* header) noexcept -> MMapError {
    auto error = MMapError::with_header(header);
    this->close_or_panic();
    return error;
}

auto MMapRaw::open_raw(std::filesystem::path const& path, bool read_only,
                       std::optional<std::size_t> create_size) noexcept -> MMapError {
    if (auto error = this->close_raw()) {
        return error;
    }
#ifdef _WIN32
    auto const raw_file_handle = ::CreateFile(path.string().c_str(),
                                              read_only ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                                              0,
                                              create_size ? OPEN_ALWAYS : OPEN_EXISTING,
                                              FILE_ATTRIBUTE_NORMAL,
                                              0);
    if (raw_file_handle == INVALID_HANDLE_VALUE || raw_file_handle == nullptr) {
        return this->close_on_error("open file handle");
    }
    this->file_handle_ = reinterpret_cast<std::intptr_t>(raw_file_handle);

    auto raw_file_size = LARGE_INTEGER{};
    if (create_size) {
        raw_file_size.QuadPart = *create_size;
        if (::SetFilePointerEx(raw_file_handle, raw_file_size, nullptr, FILE_BEGIN) == FALSE) {
            return this->close_on_error("set file size");
        }
        if (::SetEndOfFile(raw_file_handle) == FALSE) {
            return this->close_on_error("set file end");
        }
        if (::SetFilePointerEx(raw_file_handle, { .QuadPart = 0 }, nullptr, FILE_BEGIN) == FALSE) {
            return this->close_on_error("set file begin");
        }
    } else {
        if (::GetFileSizeEx(raw_file_handle, &raw_file_size) == 0) {
            return this->close_on_error("get file size");
        }
    }
    if (raw_file_size.QuadPart == 0) {
        return {};
    }
    this->file_size_ = static_cast<std::size_t>(raw_file_size.QuadPart);

    auto const raw_map_handle = ::CreateFileMapping(raw_file_handle,
                                                    0,
                                                    read_only ? PAGE_READONLY : PAGE_READWRITE,
                                                    raw_file_size.HighPart,
                                                    raw_file_size.LowPart,
                                                    0);
    if (raw_map_handle == INVALID_HANDLE_VALUE || raw_map_handle == nullptr) {
        return this->close_on_error("open map handle");
    }
    this->map_handle_ = reinterpret_cast<std::intptr_t>(raw_map_handle);

    auto const raw_data = ::MapViewOfFile(raw_map_handle,
                                          read_only ? FILE_MAP_READ : FILE_MAP_READ | FILE_MAP_WRITE,
                                          0,
                                          0,
                                          this->file_size_);
    if (raw_data == nullptr) {
        return this->close_on_error("map view");
    }
    this->map_data_ = raw_data;
    return {};
#else
    auto const raw_file_hadle = [&] {
        if (create_size) {
            return ::open(path.string().c_str(),
                          (read_only ? O_RDONLY : O_RDWR) | O_CREAT,
                          0644);
        } else {
            return ::open(path.string().c_str(),
                          read_only ? O_RDONLY : O_RDWR);
        }
    } ();
    if (raw_file_hadle == -1 || raw_file_hadle == 0) {
        return this->close_on_error("open file handle");
    }
    this->file_handle_ = static_cast<std::intptr_t>(raw_file_hadle);

    struct ::stat raw_stat = {};
    if (create_size) {
        raw_stat.st_size = *create_size;
        if (::ftruncate(raw_file_hadle, raw_stat.st_size) != 0) {
            return this->close_on_error("set file size");
        }
    } else {
        if (::fstat(raw_file_hadle, &raw_stat) != 0) {
            return this->close_on_error("get file size");
        }
    }
    if (raw_stat.st_size == 0) {
        return {};
    }
    this->file_size_ = static_cast<std::size_t>(raw_stat.st_size);

    auto const raw_data = ::mmap(nullptr,
                                 raw_stat.st_size,
                                 read_only ? PROT_READ : PROT_WRITE,
                                 MAP_SHARED,
                                 raw_file_hadle,
                                 0);
    if (raw_data == MAP_FAILED) {
        return this->close_on_error("map view");
    }
    this->map_data_ = raw_data;
    return {};
#endif
}

auto MMapRaw::sync_raw() noexcept -> void {
#ifdef _WIN32
    if (this->map_data_ != nullptr) {
        ::FlushViewOfFile(this->map_data_, this->file_size_);
        auto const raw_file_handle = reinterpret_cast<HANDLE>(this->file_handle_);
        ::FlushFileBuffers(raw_file_handle);
    }
#else
    if (this->map_data_ != nullptr) {
        ::msync(this->map_data_, this->file_size_, MS_SYNC);
        auto const raw_file_handle = static_cast<int>(this->file_handle_);
        ::fsync(raw_file_handle);
    }
#endif
}
