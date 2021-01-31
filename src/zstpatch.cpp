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
    ::fprintf(stderr, "zstpatch <in old file> <in diff file> <out new file>\n");
    return EXIT_FAILURE;
}

static int zst_patch(std::filesystem::path const& path_old,
                     std::filesystem::path const& path_diff,
                     std::filesystem::path const& path_new) noexcept {
    // mmap old file
    auto map_old = MMap<char const>();
    ::printf("Mapping old file...\n");
    if (auto error = map_old.open(path_old)) {
        return exit_mmap_error("open old file", error);
    }

    // mmap diff file
    auto map_diff = MMap<char const>();
    ::printf("Mapping diff file...\n");
    if (auto error = map_diff.open(path_diff)) {
        return exit_mmap_error("create diff file", error);
    }

    // create context and set correct params for buffer-less compression(no internal copies)
    auto const ctx = ZSTD_createDCtx();
    if (ctx == nullptr) {
        return exit_other_error("allocate compress context");
    }

    // create new dict by reference(no copies)
    ::printf("Loading dictionary...\n");
    auto const dict = ZSTD_createDDict_advanced(map_old.data(), map_old.size(),
                                                ZSTD_dlm_byRef,
                                                ZSTD_dct_rawContent,
                                                {});
    if (dict == nullptr) {
        return exit_other_error("create dictionary");
    }

    // mmap new file
    auto const new_size_ex = ZSTD_getFrameContentSize(map_diff.data(), map_diff.size());
    if (new_size_ex == ZSTD_CONTENTSIZE_UNKNOWN) {
        // TODO: we would need to stream without specific content size
        // this is not desirable as we are potentialy dealing with very large dictionary/old files
        // Solution 1: use some heuristic to fallback to streaming mode for small-ish files
        // Solution 2: implement growable mmaps
        return exit_other_error("get content size, there is no content size");
    }
    if (ZSTD_isError(new_size_ex)) {
        return exit_zstd_error("extract content size", new_size_ex);
    }
    auto map_new = MMap<char>();
    ::printf("Mapping new file...\n");
    if (auto error = map_new.create(path_new, new_size_ex)) {
        return exit_mmap_error("open new file", error);
    }

    // do decompression
    std::size_t in_pos = 0;
    std::size_t out_pos = 0;
    if (auto const error = ZSTD_decompressBegin_usingDDict(ctx, dict); ZSTD_isError(error)) {
        return exit_zstd_error("begin decompress", error);
    }
    ::printf("Decompress start...\n");
    while (auto const next_in_size = ZSTD_nextSrcSizeToDecompress(ctx)) {
        if (ZSTD_isError(next_in_size)) {
            return exit_zstd_error("next in size", next_in_size);
        }
        auto const left_in_size = map_diff.size() - in_pos;
        auto const actual_in_size = std::min(next_in_size, left_in_size);

        ::printf("\rDecompress: %-20llu", static_cast<unsigned long long>(left_in_size));
        auto const left_out_size = map_new.size() - out_pos;
        auto const next_out_size = ZSTD_decompressContinue(ctx,
                                                           map_new.data() + out_pos, left_out_size,
                                                           map_diff.data() + in_pos, actual_in_size);
        if (ZSTD_isError(next_out_size)) {
            return exit_zstd_error("decompress continue", next_out_size);
        }
        in_pos += next_in_size;
        out_pos += next_out_size;
    }
    ::printf("\rDecompress: %-20llu\n", static_cast<unsigned long long>(map_diff.size() - in_pos));
    ::printf("Done!\n");

    // free context and dict structs
    ZSTD_freeDCtx(ctx);
    ZSTD_freeDDict(dict);
    return EXIT_SUCCESS;
}


int main(int argc, char** argv) {
    if (argc != 4) {
        return exit_bad_args();
    }
    return zst_patch(argv[1], argv[2], argv[3]);
}
