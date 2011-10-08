// File: lzham.h
// See Copyright Notice and license at the end of this file.

#pragma once

// Upper byte = major version
// Lower byte = minor version
#define LZHAM_DLL_VERSION        0x1006

#ifdef LZHAM_EXPORTS
   #define LZHAM_DLL_EXPORT __declspec(dllexport)
#else
   #define LZHAM_DLL_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

   typedef unsigned char   lzham_uint8;
   typedef unsigned int    lzham_uint32;
   typedef unsigned int    lzham_bool;

   // Returns DLL version.
   LZHAM_DLL_EXPORT lzham_uint32 lzham_get_version(void);

   // User provided memory allocation

   // Custom allocation function must return pointers with LZHAM_MIN_ALLOC_ALIGNMENT (or better).
   #define LZHAM_MIN_ALLOC_ALIGNMENT sizeof(size_t) * 2

   typedef void*  (*lzham_realloc_func)(void* p, size_t size, size_t* pActual_size, bool movable, void* pUser_data);
   typedef size_t (*lzham_msize_func)(void* p, void* pUser_data);

   LZHAM_DLL_EXPORT void lzham_set_memory_callbacks(lzham_realloc_func pRealloc, lzham_msize_func pMSize, void* pUser_data);

   // Compression
   #define LZHAM_MIN_DICT_SIZE_LOG2 15
   #define LZHAM_MAX_DICT_SIZE_LOG2_X86 26
   #define LZHAM_MAX_DICT_SIZE_LOG2_X64 29

   #define LZHAM_MAX_HELPER_THREADS 16

   enum lzham_compress_status_t
   {
      LZHAM_COMP_STATUS_NOT_FINISHED = 0,
      LZHAM_COMP_STATUS_NEEDS_MORE_INPUT,

      // All the following enums must indicate failure/success.

      LZHAM_COMP_STATUS_FIRST_SUCCESS_OR_FAILURE_CODE,

      LZHAM_COMP_STATUS_SUCCESS = LZHAM_COMP_STATUS_FIRST_SUCCESS_OR_FAILURE_CODE,
      LZHAM_COMP_STATUS_FAILED,
      LZHAM_COMP_STATUS_FAILED_INITIALIZING,
      LZHAM_COMP_STATUS_INVALID_PARAMETER,
      LZHAM_COMP_STATUS_OUTPUT_BUF_TOO_SMALL,

      LZHAM_COMP_STATUS_FORCE_DWORD = 0xFFFFFFFF
   };

   enum lzham_compress_level
   {
      LZHAM_COMP_LEVEL_FASTEST = 0,
      LZHAM_COMP_LEVEL_FASTER,
      LZHAM_COMP_LEVEL_DEFAULT,
      LZHAM_COMP_LEVEL_BETTER,
      LZHAM_COMP_LEVEL_UBER,

      LZHAM_TOTAL_COMP_LEVELS,

      LZHAM_COMP_LEVEL_FORCE_DWORD = 0xFFFFFFFF
   };

   // streaming (zlib-like) interface
   typedef void *lzham_compress_state_ptr;
   enum lzham_compress_flags
   {
      LZHAM_COMP_FLAG_FORCE_POLAR_CODING = 1,
      LZHAM_COMP_FLAG_EXTREME_PARSING = 2,
      LZHAM_COMP_FLAG_DETERMINISTIC_PARSING = 4
   };

   struct lzham_compress_params
   {
      lzham_uint32 m_struct_size;
      lzham_uint32 m_dict_size_log2;
      lzham_compress_level m_level;
      lzham_uint32 m_max_helper_threads;
      lzham_uint32 m_cpucache_total_lines;
      lzham_uint32 m_cpucache_line_size;
      lzham_uint32 m_compress_flags;
   };
   LZHAM_DLL_EXPORT lzham_compress_state_ptr lzham_compress_init(const lzham_compress_params *pParams);

   // returns adler32 of source data (valid only on success).
   LZHAM_DLL_EXPORT lzham_uint32 lzham_compress_deinit(lzham_compress_state_ptr pState);

   LZHAM_DLL_EXPORT lzham_compress_status_t lzham_compress(
      lzham_compress_state_ptr pState,
      const lzham_uint8 *pIn_buf, size_t *pIn_buf_size,
      lzham_uint8 *pOut_buf, size_t *pOut_buf_size,
      lzham_bool no_more_input_bytes_flag);

   // single call interface

   LZHAM_DLL_EXPORT lzham_compress_status_t lzham_compress_memory(
      const lzham_compress_params *pParams,
      lzham_uint8* pDst_buf,
      size_t *pDst_len,
      const lzham_uint8* pSrc_buf,
      size_t src_len,
      lzham_uint32 *pAdler32);

   // Decompression
   enum lzham_decompress_status_t
   {
      LZHAM_DECOMP_STATUS_NOT_FINISHED = 0,
      LZHAM_DECOMP_STATUS_NEEDS_MORE_INPUT,

      // All the following enums must indicate failure/success.
      // TODO: Change failure to negative, success=0, needs more/not finished to >0

      LZHAM_DECOMP_STATUS_FIRST_SUCCESS_OR_FAILURE_CODE,

      LZHAM_DECOMP_STATUS_SUCCESS = LZHAM_DECOMP_STATUS_FIRST_SUCCESS_OR_FAILURE_CODE,
      LZHAM_DECOMP_STATUS_FAILED,
      LZHAM_DECOMP_STATUS_FAILED_INITIALIZING,
      LZHAM_DECOMP_STATUS_FAILED_BAD_CODE,
      LZHAM_DECOMP_STATUS_FAILED_DEST_BUF_TOO_SMALL,
      LZHAM_DECOMP_STATUS_FAILED_ADLER32,
      LZHAM_DECOMP_STATUS_INVALID_PARAMETER
   };

   // streaming (zlib-like) interface
   typedef void *lzham_decompress_state_ptr;

   struct lzham_decompress_params
   {
      lzham_uint32 m_struct_size;
      lzham_uint32 m_dict_size_log2;
      lzham_bool m_output_unbuffered;
      lzham_bool m_compute_adler32;
   };
   LZHAM_DLL_EXPORT lzham_decompress_state_ptr lzham_decompress_init(const lzham_decompress_params *pParams);

   // returns adler32 of decompressed data if compute_adler32 was true, otherwise it returns the adler32 from the compressed stream.
   LZHAM_DLL_EXPORT lzham_uint32 lzham_decompress_deinit(lzham_decompress_state_ptr pState);

   LZHAM_DLL_EXPORT lzham_decompress_status_t lzham_decompress(
      lzham_decompress_state_ptr pState,
      const lzham_uint8 *pIn_buf, size_t *pIn_buf_size,
      lzham_uint8 *pOut_buf, size_t *pOut_buf_size,
      lzham_bool no_more_input_bytes_flag);

   // single call interface
   LZHAM_DLL_EXPORT lzham_decompress_status_t lzham_decompress_memory(
      const lzham_decompress_params *pParams,
      lzham_uint8* pDst_buf,
      size_t *pDst_len,
      const lzham_uint8* pSrc_buf,
      size_t src_len,
      lzham_uint32 *pAdler32);

   // Exported function typedefs, to simplify loading the LZHAM DLL dynamically.
   typedef lzham_uint32 (*lzham_get_version_func)(void);
   typedef void (*lzham_set_memory_callbacks_func)(lzham_realloc_func pRealloc, lzham_msize_func pMSize, void* pUser_data);
   typedef lzham_compress_state_ptr (*lzham_compress_init_func)(const lzham_compress_params *pParams);
   typedef lzham_uint32 (*lzham_compress_deinit_func)(lzham_compress_state_ptr pState);
   typedef lzham_compress_status_t (*lzham_compress_func)(lzham_compress_state_ptr pState, const lzham_uint8 *pIn_buf, size_t *pIn_buf_size, lzham_uint8 *pOut_buf, size_t *pOut_buf_size, lzham_bool no_more_input_bytes_flag);
   typedef lzham_compress_status_t (*lzham_compress_memory_func)(const lzham_compress_params *pParams, lzham_uint8* pDst_buf, size_t *pDst_len, const lzham_uint8* pSrc_buf, size_t src_len, lzham_uint32 *pAdler32);
   typedef lzham_decompress_state_ptr (*lzham_decompress_init_func)(const lzham_decompress_params *pParams);
   typedef lzham_uint32 (*lzham_decompress_deinit_func)(lzham_decompress_state_ptr pState);
   typedef lzham_decompress_status_t (*lzham_decompress_func)(lzham_decompress_state_ptr pState, const lzham_uint8 *pIn_buf, size_t *pIn_buf_size, lzham_uint8 *pOut_buf, size_t *pOut_buf_size, lzham_bool no_more_input_bytes_flag);
   typedef lzham_decompress_status_t (*lzham_decompress_memory_func)(const lzham_decompress_params *pParams, lzham_uint8* pDst_buf, size_t *pDst_len, const lzham_uint8* pSrc_buf, size_t src_len, lzham_uint32 *pAdler32);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class ilzham
{
   ilzham(const ilzham &other);
   ilzham& operator= (const ilzham &rhs);

public:
   ilzham() { clear(); }

   virtual ~ilzham() { }
   virtual bool load() = 0;
   virtual void unload() = 0;
   virtual bool is_loaded() = 0;

   void clear()
   {
      lzham_get_version = NULL;
      lzham_set_memory_callbacks = NULL;
      lzham_compress_init = NULL;
      lzham_compress_deinit = NULL;
      lzham_compress = NULL;
      lzham_compress_memory = NULL;
      lzham_decompress_init = NULL;
      lzham_decompress_deinit = NULL;
      lzham_decompress = NULL;
      lzham_decompress_memory = NULL;
   }

   lzham_get_version_func           lzham_get_version;
   lzham_set_memory_callbacks_func  lzham_set_memory_callbacks;
   lzham_compress_init_func         lzham_compress_init;
   lzham_compress_deinit_func       lzham_compress_deinit;
   lzham_compress_func              lzham_compress;
   lzham_compress_memory_func       lzham_compress_memory;
   lzham_decompress_init_func       lzham_decompress_init;
   lzham_decompress_deinit_func     lzham_decompress_deinit;
   lzham_decompress_func            lzham_decompress;
   lzham_decompress_memory_func     lzham_decompress_memory;
};
#endif

// Copyright (c) 2009-2011 Richard Geldreich, Jr. <richgel99@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
