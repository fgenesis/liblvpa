// File: lzham_decomp.h
// See Copyright Notice and license at the end of include/lzham.h
#pragma once
#include "lzham.h"

namespace lzham
{
   void lzham_lib_set_memory_callbacks(lzham_realloc_func pRealloc, lzham_msize_func pMSize, void* pUser_data);
   
   lzham_decompress_state_ptr lzham_lib_decompress_init(const lzham_decompress_params *pParams);

   lzham_uint32 lzham_lib_decompress_deinit(lzham_decompress_state_ptr p);

   lzham_decompress_status_t lzham_lib_decompress(
      lzham_decompress_state_ptr p,
      const lzham_uint8 *pIn_buf, size_t *pIn_buf_size, 
      lzham_uint8 *pOut_buf, size_t *pOut_buf_size,
      lzham_bool no_more_input_bytes_flag);
      
   lzham_decompress_status_t lzham_lib_decompress_memory(const lzham_decompress_params *pParams, lzham_uint8* pDst_buf, size_t *pDst_len, const lzham_uint8* pSrc_buf, size_t src_len, lzham_uint32 *pAdler32);

} // namespace lzham
