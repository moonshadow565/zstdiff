#pragma once
#include <filesystem>
#include <optional>
#include <span>
#include <type_traits>

struct MMapError {
    char const* header = {};
    int errnum = {};

    static auto with_header(char const* header) noexcept -> MMapError;

    [[nodiscard]] inline explicit operator bool() const noexcept {
        return this->errnum != 0;
    }
    [[nodiscard]] inline bool operator!() const noexcept {
        return this->errnum == 0;
    }
};

struct MMapRaw {
protected:
    std::intptr_t file_handle_ = {};
    std::size_t file_size_ = {};
    std::intptr_t map_handle_ = {};
    void* map_data_ = {};

    [[nodiscard]] auto open_raw(std::filesystem::path const& path, bool read_only,
                                std::optional<std::size_t> create_size = {}) noexcept -> MMapError;
    [[nodiscard]] auto close_raw(std::optional<std::size_t> trunc_size = {}) noexcept -> MMapError;
    auto close_or_panic() noexcept -> void;
    auto sync_raw() noexcept -> void;

private:
    [[nodiscard]] auto close_on_error(char const* header) noexcept -> MMapError;
};

template <typename CharType>
struct MMap : private MMapRaw {
    [[nodiscard]] inline MMap() noexcept = default;
    inline MMap(MMap const& other) = delete;
    [[nodiscard]] inline MMap(MMap&& other) noexcept {
        std::swap(static_cast<MMapRaw&>(*this), static_cast<MMapRaw&>(other));
    }
    inline MMap& operator=(MMap const& other) = delete;
    inline MMap& operator=(MMap&& other) noexcept {
        MMap tmp = static_cast<MMap&&>(other);
        std::swap(static_cast<MMapRaw&>(*this), static_cast<MMapRaw&>(tmp));
        return *this;
    }
    inline ~MMap() noexcept {
        this->close_or_panic();
    }

    [[nodiscard]] inline auto open(std::filesystem::path const& path) noexcept -> MMapError {
        return this->open_raw(path, std::is_const_v<CharType>);
    }
    [[nodiscard]] inline auto create(std::filesystem::path const& path,
                                     std::size_t size) noexcept -> MMapError requires (!std::is_const_v<CharType>) {
        return this->open_raw(path, std::is_const_v<CharType>, size);
    }
    inline auto sync() noexcept -> void {
        this->sync_raw();
    }
    [[nodiscard]] inline auto close() noexcept -> MMapError {
        return this->close_raw();
    }
    [[nodiscard]] inline auto close(std::size_t size) noexcept -> MMapError requires (!std::is_const_v<CharType>) {
        return this->close_raw(size);
    }

    [[nodiscard]] inline auto data() const noexcept -> CharType* {
        return reinterpret_cast<CharType*>(this->map_data_);
    }
    [[nodiscard]] inline auto size() const noexcept {
        return this->file_size_ / sizeof(CharType);
    }
    [[nodiscard]] inline auto span() const noexcept -> std::span<CharType> {
        return std::span { reinterpret_cast<CharType*>(this->map_data_), this->file_size_ / sizeof(CharType) };
    }

    [[nodiscard]] inline explicit operator bool() const noexcept {
        return this->file_handle_ != 0;
    }
    [[nodiscard]] inline bool operator!() const noexcept {
        return this->file_handle_ == 0;
    }
};
