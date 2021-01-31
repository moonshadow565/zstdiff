#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <bit>
#include "mmap.hpp"
#include "zstd.h"

static int exit_mmap_error(char const* from, MMapError const& error) noexcept {
    ::fprintf(stderr, "Failed to %s from %s because %d(%s)\n",
              from, error.header, error.errnum, ::strerror(error.errnum));
    return EXIT_FAILURE;
}

static int exit_zstd_error(char const* from, std::size_t error) noexcept {
    ::fprintf(stderr, "Failed to %s because %s\n", from, ZSTD_getErrorName(error));
    return EXIT_FAILURE;
}

static int exit_other_error(char const* msg) {
    ::fprintf(stderr, "Failed to %s\n", msg);
    return EXIT_FAILURE;
}

static int exit_bad_args() noexcept {
    ::fprintf(stderr, "zstdiff <in old file> <in new file> <out diff file> <opt compress level>\n");
    return EXIT_FAILURE;
}

static void print_progress(std::size_t done, std::size_t total) {
    constexpr char const* const unit_name[] = {
        "B", "KB", "MB", "GB",
    };
    auto const remain = total - done;
    std::size_t unit_index = 0;
    std::size_t unit_mult = 1;
    while (remain / (unit_mult * 1024u) > 1 && unit_index != 3) {
        unit_mult *= 1024u;
        ++unit_index;
    }
    ::printf("\rRemain: %-5llu %s",
             static_cast<unsigned long long>(remain / unit_mult),
             unit_name[unit_index]);
}

static int zst_diff(std::filesystem::path const& path_old,
                    std::filesystem::path const& path_new,
                    std::filesystem::path const& path_diff,
                    int level) noexcept {
    auto map_old = MMap<char const>();
    ::printf("Maping old file...\n");
    if (auto error = map_old.open(path_old)) {
        return exit_mmap_error("open old file", error);
    }

    auto map_new = MMap<char const>();
    ::printf("Maping new file...\n");
    if (auto error = map_new.open(path_new)) {
        return exit_mmap_error("open new file", error);
    }

    // create context and set correct params for buffer-less compression(no internal copies)
    auto const ctx = ZSTD_createCCtx();
    if (ctx == nullptr) {
        return exit_other_error("allocate compress context");
    }
    // create and set dict as raw content, by reference(no copies)
    auto cparams = ZSTD_getCParams(level, map_new.size(), map_old.size());
    auto const dict_size_plus = static_cast<std::uint64_t>(map_old.size() + 1024);
    cparams.windowLog = static_cast<unsigned>(64u - std::countl_zero(dict_size_plus) - 1);
    ::printf("Loading dictionary...\n");
    auto const dict = ZSTD_createCDict_advanced(map_old.data(), map_old.size(),
                                                ZSTD_dlm_byRef,
                                                ZSTD_dct_rawContent,
                                                cparams,
                                                {});
    if (dict == nullptr) {
        return exit_other_error("create dictionary");
    }
    if (auto const error = ZSTD_CCtx_refCDict(ctx, dict); ZSTD_isError(error)) {
        return exit_zstd_error("set refCDict", error);
    }

    // create/open diff file and resize it to estimated size
    auto const size_diff_estimated = ZSTD_compressBound(map_new.size());
    auto map_diff = MMap<char>();
    ::printf("Maping diff file...\n");
    if (auto error = map_diff.create(path_diff, size_diff_estimated)) {
        return exit_mmap_error("create diff file", error);
    }

    // do compression
    std::size_t in_pos = 0;
    std::size_t out_pos = 0;
    ::printf("Compress start...\n");
    auto const fparams = ZSTD_frameParameters {
            .contentSizeFlag = 1,
            .checksumFlag = 1,
            .noDictIDFlag = 0,
    };
    if (auto const error = ZSTD_compressBegin_usingCDict_advanced(ctx, dict, fparams, map_new.size());
            ZSTD_isError(error)) {
        return exit_zstd_error("compress begin", error);
    }
    auto const block_size = ZSTD_getBlockSize(ctx);
    while (auto const in_left = map_new.size() - in_pos) {
        auto const to_read = std::min(block_size, in_left);
        auto result = std::size_t{};
        if (in_left <= block_size) {
            result = ZSTD_compressEnd(ctx,
                                      map_diff.data() + out_pos, map_diff.size() - out_pos,
                                      map_new.data() + in_pos, to_read);
        } else {
            result = ZSTD_compressContinue(ctx,
                                           map_diff.data() + out_pos, map_diff.size() - out_pos,
                                           map_new.data() + in_pos, to_read);
        }
        if (ZSTD_isError(result)) {
            ZSTD_freeCCtx(ctx);
            ZSTD_freeCDict(dict);
            [[maybe_unused]] auto const unused_ = map_diff.close(0);
            ::printf("\n");
            return exit_zstd_error("compress file", result);
        }
        in_pos += to_read;
        out_pos += result;
        print_progress(in_pos, map_new.size());
    }
    // truncate and close the file
    ::printf("\nFlush diff file...\n");
    if (auto error = map_diff.close(out_pos)) {
        return exit_mmap_error("close diff file", error);
    }
    ::printf("Done!\n");

    // free context and dict structs
    ZSTD_freeCCtx(ctx);
    ZSTD_freeCDict(dict);

    return EXIT_SUCCESS;
}


int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) {
        return exit_bad_args();
    }
    return zst_diff(argv[1], argv[2], argv[3], argc == 5 ? ::atoi(argv[4]) : 0);
}
