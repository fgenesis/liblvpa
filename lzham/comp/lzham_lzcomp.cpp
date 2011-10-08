// File: lzham_lzcomp.cpp
// See Copyright Notice and license at the end of include/lzham.h
#include "lzham_core.h"
#include "lzham.h"
#include "lzham_lzcomp_internal.h"

using namespace lzham;

namespace lzham
{
   struct lzham_compress_state
   {
      // task_pool requires 8 or 16 alignment
      task_pool m_tp;
      lzcompressor m_compressor;
            
      uint m_dict_size_log2;

      const uint8 *m_pIn_buf;
      size_t *m_pIn_buf_size;
      uint8 *m_pOut_buf;
      size_t *m_pOut_buf_size;
            
      size_t m_comp_data_ofs;
      
      bool m_flushed_compressor;
      
      lzham_compress_params m_params;

      lzham_compress_status_t m_status;
   };
   
   static lzham_compress_status_t create_init_params(lzcompressor::init_params &params, const lzham_compress_params *pParams)
   {
      if ((pParams->m_dict_size_log2 < CLZBase::cMinDictSizeLog2) || (pParams->m_dict_size_log2 > CLZBase::cMaxDictSizeLog2))
         return LZHAM_COMP_STATUS_INVALID_PARAMETER;
      
      if (pParams->m_cpucache_total_lines)
      {
         if (!math::is_power_of_2(pParams->m_cpucache_line_size))
         {
            return LZHAM_COMP_STATUS_INVALID_PARAMETER;
         }
      }
         
      params.m_dict_size_log2 = pParams->m_dict_size_log2;
      params.m_max_helper_threads = LZHAM_MIN(LZHAM_MAX_HELPER_THREADS, pParams->m_max_helper_threads);
      params.m_num_cachelines = pParams->m_cpucache_total_lines;
      params.m_cacheline_size = pParams->m_cpucache_line_size;
      params.m_lzham_compress_flags = pParams->m_compress_flags;
      
      switch (pParams->m_level)
      {
         case LZHAM_COMP_LEVEL_FASTEST:   params.m_compression_level = cCompressionLevelFastest; break;
         case LZHAM_COMP_LEVEL_FASTER:    params.m_compression_level = cCompressionLevelFaster; break;
         case LZHAM_COMP_LEVEL_DEFAULT:   params.m_compression_level = cCompressionLevelDefault; break;
         case LZHAM_COMP_LEVEL_BETTER:    params.m_compression_level = cCompressionLevelBetter; break;
         case LZHAM_COMP_LEVEL_UBER:      params.m_compression_level = cCompressionLevelUber; break;
         default:
            return LZHAM_COMP_STATUS_INVALID_PARAMETER;
      };
   
      return LZHAM_COMP_STATUS_SUCCESS;
   }
   
   lzham_compress_state_ptr lzham_lib_compress_init(const lzham_compress_params *pParams)
   {
      if ((!pParams) || (pParams->m_struct_size != sizeof(lzham_compress_params)))   
         return NULL;

      if ((pParams->m_dict_size_log2 < CLZBase::cMinDictSizeLog2) || (pParams->m_dict_size_log2 > CLZBase::cMaxDictSizeLog2))
         return NULL;
      
      lzcompressor::init_params params;
      lzham_compress_status_t status = create_init_params(params, pParams);
      if (status != LZHAM_COMP_STATUS_SUCCESS)
         return NULL;
      
      lzham_compress_state *pState = lzham_new<lzham_compress_state>();
      if (!pState)
         return NULL;

      pState->m_params = *pParams;
      
      pState->m_pIn_buf = NULL;
      pState->m_pIn_buf_size = NULL;
      pState->m_pOut_buf = NULL;
      pState->m_pOut_buf_size = NULL;
      pState->m_status = LZHAM_COMP_STATUS_NOT_FINISHED;
      pState->m_comp_data_ofs = 0;
      pState->m_flushed_compressor = false;
      
      if (params.m_max_helper_threads)
      {
         if (!pState->m_tp.init(params.m_max_helper_threads))
         {
            lzham_delete(pState);
            return NULL;
         }
         if (pState->m_tp.get_num_threads() >= params.m_max_helper_threads)
         {
            params.m_pTask_pool = &pState->m_tp;
         }
         else
         {
            params.m_max_helper_threads = 0;
         }
      }
                  
      if (!pState->m_compressor.init(params))
      {
         lzham_delete(pState);
         return NULL;
      }
      
      return pState;
   }

   lzham_uint32 lzham_lib_compress_deinit(lzham_compress_state_ptr p)
   {
      lzham_compress_state *pState = static_cast<lzham_compress_state *>(p);
      if (!pState)
         return 0;  

      uint32 adler32 = pState->m_compressor.get_src_adler32();

      lzham_delete(pState);

      return adler32;
   }

   lzham_compress_status_t lzham_lib_compress(
      lzham_compress_state_ptr p,
      const lzham_uint8 *pIn_buf, size_t *pIn_buf_size, 
      lzham_uint8 *pOut_buf, size_t *pOut_buf_size,
      lzham_bool no_more_input_bytes_flag)
   {
      lzham_compress_state *pState = static_cast<lzham_compress_state*>(p);

      if ((!pState) || (!pState->m_params.m_dict_size_log2) || (pState->m_status >= LZHAM_COMP_STATUS_FIRST_SUCCESS_OR_FAILURE_CODE) || (!pIn_buf_size) || (!pOut_buf_size))
      {
         return LZHAM_COMP_STATUS_INVALID_PARAMETER;
      }
      
      if ((*pIn_buf_size) && (!pIn_buf))
      {
         return LZHAM_COMP_STATUS_INVALID_PARAMETER;
      }
      
      if ((!*pOut_buf_size) || (!pOut_buf))
      {
         return LZHAM_COMP_STATUS_INVALID_PARAMETER;
      }
      
      byte_vec &comp_data = pState->m_compressor.get_compressed_data();
      if (pState->m_comp_data_ofs < comp_data.size())
      {
         *pIn_buf_size = 0;
         *pOut_buf_size = LZHAM_MIN(comp_data.size() - pState->m_comp_data_ofs, *pOut_buf_size);
                  
         memcpy(pOut_buf, comp_data.get_ptr() + pState->m_comp_data_ofs, *pOut_buf_size);
         
         pState->m_comp_data_ofs += *pOut_buf_size;
         
         pState->m_status = LZHAM_COMP_STATUS_NOT_FINISHED;
         return pState->m_status;
      }
      
      comp_data.try_resize(0);
      pState->m_comp_data_ofs = 0;
      
      if (pState->m_flushed_compressor)
      {
         if ((*pIn_buf_size) || (!no_more_input_bytes_flag))
         {
            pState->m_status = LZHAM_COMP_STATUS_INVALID_PARAMETER;
            return pState->m_status;
         }
         
         *pIn_buf_size = 0;
         *pOut_buf_size = 0;
         
         pState->m_status = LZHAM_COMP_STATUS_SUCCESS;
         return pState->m_status;
      }
      
      const size_t cMaxBytesToPutPerIteration = 4*1024*1024;
      size_t bytes_to_put = LZHAM_MIN(cMaxBytesToPutPerIteration, *pIn_buf_size);
      const bool consumed_entire_input_buf = (bytes_to_put == *pIn_buf_size);

      if (bytes_to_put)
      {
         if (!pState->m_compressor.put_bytes(pIn_buf, (uint)bytes_to_put))
         {
            *pIn_buf_size = 0;
            *pOut_buf_size = 0;
            
            pState->m_status = LZHAM_COMP_STATUS_FAILED;
            return pState->m_status;
         }
      }
      
      if ((consumed_entire_input_buf) && (no_more_input_bytes_flag) && (!pState->m_flushed_compressor))
      {
         if (!pState->m_compressor.put_bytes(NULL, 0))
         {
            *pIn_buf_size = 0;
            *pOut_buf_size = 0;

            pState->m_status = LZHAM_COMP_STATUS_FAILED;
            return pState->m_status;
         }  
         pState->m_flushed_compressor = true;  
      }
                  
      *pIn_buf_size = bytes_to_put;
      
      *pOut_buf_size = LZHAM_MIN(comp_data.size() - pState->m_comp_data_ofs, *pOut_buf_size);   
      if (*pOut_buf_size)
      {
         memcpy(pOut_buf, comp_data.get_ptr() + pState->m_comp_data_ofs, *pOut_buf_size);

         pState->m_comp_data_ofs += *pOut_buf_size;
      }
      
      if ((no_more_input_bytes_flag) && (pState->m_flushed_compressor) && (pState->m_comp_data_ofs >= comp_data.size()))
         pState->m_status = LZHAM_COMP_STATUS_SUCCESS;
      else if ((consumed_entire_input_buf) && (!no_more_input_bytes_flag) && (pState->m_comp_data_ofs >= comp_data.size()))
         pState->m_status = LZHAM_COMP_STATUS_NEEDS_MORE_INPUT;
      else
         pState->m_status = LZHAM_COMP_STATUS_NOT_FINISHED;
      
      return pState->m_status;  
   }      

   lzham_compress_status_t lzham_lib_compress_memory(const lzham_compress_params *pParams, lzham_uint8* pDst_buf, size_t *pDst_len, const lzham_uint8* pSrc_buf, size_t src_len, lzham_uint32 *pAdler32)
   {
      if ((!pParams) || (!pDst_len))
         return LZHAM_COMP_STATUS_INVALID_PARAMETER;

      if (src_len)
      {
         if (!pSrc_buf)
            return LZHAM_COMP_STATUS_INVALID_PARAMETER;
      }

      if (sizeof(size_t) > sizeof(uint32))
      {
         if (src_len > UINT32_MAX)
            return LZHAM_COMP_STATUS_INVALID_PARAMETER;
      }

      lzcompressor::init_params params;
      lzham_compress_status_t status = create_init_params(params, pParams);
      if (status != LZHAM_COMP_STATUS_SUCCESS)
         return status;

      task_pool *pTP = NULL;
      if (params.m_max_helper_threads)
      {
         pTP = lzham_new<task_pool>();
         if (!pTP->init(params.m_max_helper_threads))   
            return LZHAM_COMP_STATUS_FAILED;

         params.m_pTask_pool = pTP;
      }

      lzcompressor *pCompressor = lzham_new<lzcompressor>();
      if (!pCompressor)
      {
         lzham_delete(pTP);
         return LZHAM_COMP_STATUS_FAILED;
      }
      
      if (!pCompressor->init(params))
      {
         lzham_delete(pTP);
         lzham_delete(pCompressor);
         return LZHAM_COMP_STATUS_INVALID_PARAMETER;
      }

      if (src_len)
      {
         if (!pCompressor->put_bytes(pSrc_buf, static_cast<uint32>(src_len)))
         {
            *pDst_len = 0;
            lzham_delete(pTP);
            lzham_delete(pCompressor);
            return LZHAM_COMP_STATUS_FAILED;
         }
      }

      if (!pCompressor->put_bytes(NULL, 0))
      {
         *pDst_len = 0;
         lzham_delete(pTP);
         lzham_delete(pCompressor);
         return LZHAM_COMP_STATUS_FAILED;
      }

      const byte_vec &comp_data = pCompressor->get_compressed_data();

      size_t dst_buf_size = *pDst_len;
      *pDst_len = comp_data.size();

      if (pAdler32)
         *pAdler32 = pCompressor->get_src_adler32();

      if (comp_data.size() > dst_buf_size)
      {
         lzham_delete(pTP);
         lzham_delete(pCompressor);
         return LZHAM_COMP_STATUS_OUTPUT_BUF_TOO_SMALL;
      }

      memcpy(pDst_buf, comp_data.get_ptr(), comp_data.size());

      lzham_delete(pTP);
      lzham_delete(pCompressor);
      return LZHAM_COMP_STATUS_SUCCESS;  
   }

} // namespace lzham
