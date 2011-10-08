// File: lzham_comp.h
// See Copyright Notice and license at the end of include/lzham.h
#pragma once
#include "lzham.h"

namespace lzham
{
   lzham_compress_state_ptr lzham_lib_compress_init(const lzham_compress_params *pParams);
   
   lzham_uint32 lzham_lib_compress_deinit(lzham_compress_state_ptr p);

   lzham_compress_status_t lzham_lib_compress(
      lzham_compress_state_ptr p,
      const lzham_uint8 *pIn_buf, size_t *pIn_buf_size, 
      lzham_uint8 *pOut_buf, size_t *pOut_buf_size,
      lzham_bool no_more_input_bytes_flag);
   
   lzham_compress_status_t lzham_lib_compress_memory(const lzham_compress_params *pParams, lzham_uint8* pDst_buf, size_t *pDst_len, const lzham_uint8* pSrc_buf, size_t src_len, lzham_uint32 *pAdler32);

} // namespace lzham
