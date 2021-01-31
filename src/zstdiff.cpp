#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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

static int zst_diff(std::filesystem::path const& path_old,
                    std::filesystem::path const& path_new,
                    std::filesystem::path const& path_diff,
                    int level) noexcept {
    // mmap old file
    auto map_old = MMap<char const>();
    if (auto error = map_old.open(path_old)) {
        return exit_mmap_error("open old file", error);
    }

    // mmap new file
    auto map_new = MMap<char const>();
    if (auto error = map_new.open(path_new)) {
        return exit_mmap_error("open new file", error);
    }


    // create context and set correct params for buffer-less compression(no internal copies)
    auto const ctx = ZSTD_createCCtx();
    if (ctx == nullptr) {
        return exit_other_error("allocate compress context");
    }
    if (auto const error = ZSTD_CCtx_setPledgedSrcSize(ctx, map_new.size()); ZSTD_isError(error)) {
        return exit_zstd_error("set setPledgedSrcSize", error);
    }
    if (auto const error = ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, 1); ZSTD_isError(error)) {
        return exit_zstd_error("set compressionLevel", level);
    }
    if (auto const error = ZSTD_CCtx_setParameter(ctx, ZSTD_c_checksumFlag, 1); ZSTD_isError(error)) {
        return exit_zstd_error("set checksumFlag", error);
    }
    if (auto const error = ZSTD_CCtx_setParameter(ctx, ZSTD_c_stableInBuffer, 1); ZSTD_isError(error)) {
        return exit_zstd_error("set stableInBuffer", error);
    }
    if (auto const error = ZSTD_CCtx_setParameter(ctx, ZSTD_c_stableOutBuffer, 1); ZSTD_isError(error)) {
        return exit_zstd_error("set stableOutBuffer", error);
    }

    // create and set dict as raw content, by reference(no copies)
    auto params = ZSTD_getParams(level, map_new.size(), map_old.size());
    auto const dict = ZSTD_createCDict_advanced(map_old.data(), map_old.size(),
                                                ZSTD_dlm_byRef,
                                                ZSTD_dct_rawContent,
                                                params.cParams,
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
    if (auto error = map_diff.create(path_diff, size_diff_estimated)) {
        return exit_mmap_error("create diff file", error);
    }

    // do compression
    std::size_t in_pos = 0;
    std::size_t out_pos = 0;
    while (auto const result = ZSTD_compressStream2_simpleArgs(ctx,
                                                               map_diff.data(), map_diff.size(), &out_pos,
                                                               map_new.data(), map_new.size(), &in_pos,
                                                               ZSTD_e_end)) {
        if (ZSTD_isError(result)) {
            ZSTD_freeCCtx(ctx);
            ZSTD_freeCDict(dict);
            [[maybe_unused]] auto const unused_ = map_diff.close(0);
            return exit_zstd_error("compress file", result);
        }
    }
    // truncate and close the file
    if (auto error = map_diff.close(out_pos)) {
        return exit_mmap_error("close diff file", error);
    }

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
