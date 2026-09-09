#pragma once
#define MINIZ_EXPORT

// Use the same portable inflater on Host and ESP; do not alias the chip ROM.
#define MINIZ_NO_MALLOC
#define MINIZ_NO_STDIO
#define MINIZ_HAS_64BIT_REGISTERS 0
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#define MINIZ_LITTLE_ENDIAN 1
#define tinfl_decompress zectrix_tinfl_decompress
#define tinfl_decompress_mem_to_heap zectrix_tinfl_decompress_mem_to_heap
#define tinfl_decompress_mem_to_mem zectrix_tinfl_decompress_mem_to_mem
#define tinfl_decompress_mem_to_callback zectrix_tinfl_decompress_mem_to_callback
