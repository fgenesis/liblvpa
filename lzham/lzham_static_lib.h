#pragma once

#define LZHAM_STATIC_LIB 1

#include "lzham.h"
#include "lzham_decomp.h"
#include "lzham_comp.h"

// FIXME: These inlines are a workaround for the lack of a static lib project. None of the stuff below is actually C-compatible.

#ifdef __cplusplus
extern "C" {
#endif

inline lzham_uint32 lzham_get_version(void)
{
   return LZHAM_DLL_VERSION;
}

inline void lzham_set_memory_callbacks(lzham_realloc_func pRealloc, lzham_msize_func pMSize, void* pUser_data)
{
   lzham::lzham_lib_set_memory_callbacks(pRealloc, pMSize, pUser_data);
}

inline lzham_decompress_state_ptr lzham_decompress_init(const lzham_decompress_params *pParams)
{
   return lzham::lzham_lib_decompress_init(pParams);
}

inline lzham_uint32 lzham_decompress_deinit(lzham_decompress_state_ptr p)
{
   return lzham::lzham_lib_decompress_deinit(p);
}

inline lzham_decompress_status_t lzham_decompress(
   lzham_decompress_state_ptr p,
   const lzham_uint8 *pIn_buf, size_t *pIn_buf_size, 
   lzham_uint8 *pOut_buf, size_t *pOut_buf_size,
   lzham_bool no_more_input_bytes_flag)
{
   return lzham::lzham_lib_decompress(p, pIn_buf, pIn_buf_size, pOut_buf, pOut_buf_size, no_more_input_bytes_flag);
}   

inline lzham_decompress_status_t lzham_decompress_memory(const lzham_decompress_params *pParams, lzham_uint8* pDst_buf, size_t *pDst_len, const lzham_uint8* pSrc_buf, size_t src_len, lzham_uint32 *pAdler32)
{
   return lzham::lzham_lib_decompress_memory(pParams, pDst_buf, pDst_len, pSrc_buf, src_len, pAdler32);
}

inline lzham_compress_state_ptr lzham_compress_init(const lzham_compress_params *pParams)
{
   return lzham::lzham_lib_compress_init(pParams);
}

inline lzham_uint32 lzham_compress_deinit(lzham_compress_state_ptr p)
{
   return lzham::lzham_lib_compress_deinit(p);
}

inline lzham_compress_status_t lzham_compress(
   lzham_compress_state_ptr p,
   const lzham_uint8 *pIn_buf, size_t *pIn_buf_size, 
   lzham_uint8 *pOut_buf, size_t *pOut_buf_size,
   lzham_bool no_more_input_bytes_flag)
{
   return lzham::lzham_lib_compress(p, pIn_buf, pIn_buf_size, pOut_buf, pOut_buf_size, no_more_input_bytes_flag);
}   

inline lzham_compress_status_t lzham_compress_memory(const lzham_compress_params *pParams, lzham_uint8* pDst_buf, size_t *pDst_len, const lzham_uint8* pSrc_buf, size_t src_len, lzham_uint32 *pAdler32)
{
   return lzham::lzham_lib_compress_memory(pParams, pDst_buf, pDst_len, pSrc_buf, src_len, pAdler32);
}

#ifdef __cplusplus
}
#endif

class lzham_static_lib : public ilzham
{
   lzham_static_lib(const lzham_static_lib &other);
   lzham_static_lib& operator= (const lzham_static_lib &rhs);

public:
   lzham_static_lib() : ilzham() { }

   virtual ~lzham_static_lib() { }
   
   virtual bool load()
   {
      this->lzham_get_version = ::lzham_get_version;
      this->lzham_set_memory_callbacks = ::lzham_set_memory_callbacks;
      this->lzham_compress_init = ::lzham_compress_init;
      this->lzham_compress_deinit = ::lzham_compress_deinit;
      this->lzham_compress = ::lzham_compress;
      this->lzham_compress_memory = ::lzham_compress_memory;
      this->lzham_decompress_init = ::lzham_decompress_init;
      this->lzham_decompress_deinit = ::lzham_decompress_deinit;
      this->lzham_decompress = ::lzham_decompress;
      this->lzham_decompress_memory = ::lzham_decompress_memory;
      return true;
   }
   
   virtual void unload()
   {
      clear();
   }
   
   virtual bool is_loaded() { return lzham_get_version != NULL; }
};
