cmake_minimum_required(VERSION 3.5)

project(zstdiff LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(zstd STATIC
    zstd/lib/common/debug.c
    zstd/lib/common/entropy_common.c
    zstd/lib/common/error_private.c
    zstd/lib/common/fse_decompress.c
    zstd/lib/common/pool.c
    zstd/lib/common/threading.c
    zstd/lib/common/xxhash.c
    zstd/lib/common/zstd_common.c
    zstd/lib/compress/fse_compress.c
    zstd/lib/compress/hist.c
    zstd/lib/compress/huf_compress.c
    zstd/lib/compress/zstd_compress.c
    zstd/lib/compress/zstd_compress_literals.c
    zstd/lib/compress/zstd_compress_sequences.c
    zstd/lib/compress/zstd_compress_superblock.c
    zstd/lib/compress/zstd_double_fast.c
    zstd/lib/compress/zstd_fast.c
    zstd/lib/compress/zstd_lazy.c
    zstd/lib/compress/zstd_ldm.c
    zstd/lib/compress/zstdmt_compress.c
    zstd/lib/compress/zstd_opt.c
    zstd/lib/decompress/huf_decompress.c
    zstd/lib/decompress/zstd_ddict.c
    zstd/lib/decompress/zstd_decompress_block.c
    zstd/lib/decompress/zstd_decompress.c
    zstd/lib/zstd.h
    )
target_include_directories(zstd PUBLIC zstd/lib)
target_compile_definitions(zstd PUBLIC -DZSTD_STATIC_LINKING_ONLY)

add_library(mmap STATIC src/mmap.hpp src/mmap.cpp)
target_include_directories(mmap PUBLIC src)

add_executable(zstdiff src/zstdiff.cpp)
target_link_libraries(zstdiff PRIVATE zstd mmap)

add_executable(zstpatch src/zstpatch.cpp)
target_link_libraries(zstpatch PRIVATE zstd mmap)
