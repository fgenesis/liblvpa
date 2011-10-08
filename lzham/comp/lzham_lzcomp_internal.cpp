// File: lzham_lzcomp_internal.cpp
// See Copyright Notice and license at the end of include/lzham.h
#include "lzham_core.h"
#include "lzham_lzcomp_internal.h"
#include "lzham_checksum.h"
#include "lzham_timer.h"
#include "lzham_lzbase.h"
#include <string.h>

// Update and print high-level coding statistics if set to 1.
// TODO: Add match distance coding statistics.
#define LZHAM_UPDATE_STATS                   0

// Only parse on the main thread, for easier debugging.
#define LZHAM_FORCE_SINGLE_THREADED_PARSING  0

// Verify all computed match costs against the generic/slow state::get_cost() method.
#define LZHAM_VERIFY_MATCH_COSTS             0

namespace lzham
{
   static comp_settings s_settings[cCompressionLevelCount] =
   {
      // cCompressionLevelFastest
      {
         8,                               // m_fast_bytes
         true,                            // m_fast_adaptive_huffman_updating
         true,                            // m_use_polar_codes
         1,                               // m_match_accel_max_matches_per_probe
         2,                               // m_match_accel_max_probes
      },
      // cCompressionLevelFaster
      {
         24,                              // m_fast_bytes
         true,                            // m_fast_adaptive_huffman_updating
         true,                            // m_use_polar_codes
         6,                               // m_match_accel_max_matches_per_probe
         12,                              // m_match_accel_max_probes
      },
      // cCompressionLevelDefault
      {
         32,                              // m_fast_bytes
         false,                           // m_fast_adaptive_huffman_updating
         true,                            // m_use_polar_codes
         UINT_MAX,                        // m_match_accel_max_matches_per_probe
         16,                              // m_match_accel_max_probes
      },
      // cCompressionLevelBetter
      {
         48,                              // m_fast_bytes
         false,                           // m_fast_adaptive_huffman_updating
         false,                           // m_use_polar_codes
         UINT_MAX,                        // m_match_accel_max_matches_per_probe
         32,                              // m_match_accel_max_probes
      },
      // cCompressionLevelUber
      {
         64,                              // m_fast_bytes
         false,                           // m_fast_adaptive_huffman_updating
         false,                           // m_use_polar_codes
         UINT_MAX,                        // m_match_accel_max_matches_per_probe
         cMatchAccelMaxSupportedProbes,   // m_match_accel_max_probes
      }
   };

   uint lzcompressor::lzdecision::get_match_dist(const state& cur_state) const
   {
      if (!is_match())
         return 0;
      else if (is_rep())
      {
         int index = -m_dist - 1;
         LZHAM_ASSERT(index < CLZBase::cMatchHistSize);
         return cur_state.m_match_hist[index];
      }
      else
         return m_dist;
   }

   lzcompressor::state::state()
   {
      clear();
   }

   void lzcompressor::state::clear()
   {
      m_cur_ofs = 0;
      m_cur_state = 0;
      m_block_start_dict_ofs = 0;

      for (uint i = 0; i < 2; i++)
      {
         m_rep_len_table[i].clear();
         m_large_len_table[i].clear();
      }
      m_main_table.clear();
      m_dist_lsb_table.clear();

      for (uint i = 0; i < (1 << CLZBase::cNumLitPredBits); i++)
         m_lit_table[i].clear();

      for (uint i = 0; i < (1 << CLZBase::cNumDeltaLitPredBits); i++)
         m_delta_lit_table[i].clear();

      m_match_hist[0] = 1;
      m_match_hist[1] = 1;
      m_match_hist[2] = 1;
      m_match_hist[3] = 1;
   }

   bool lzcompressor::state::init(CLZBase& lzbase, bool fast_adaptive_huffman_updating, bool use_polar_codes)
   {
      m_cur_ofs = 0;
      m_cur_state = 0;

      for (uint i = 0; i < 2; i++)
      {
         if (!m_rep_len_table[i].init(true, CLZBase::cMaxMatchLen - CLZBase::cMinMatchLen + 1, fast_adaptive_huffman_updating, use_polar_codes)) return false;
         if (!m_large_len_table[i].init(true, CLZBase::cLZXNumSecondaryLengths, fast_adaptive_huffman_updating, use_polar_codes)) return false;
      }
      if (!m_main_table.init(true, CLZBase::cLZXNumSpecialLengths + (lzbase.m_num_lzx_slots - CLZBase::cLZXLowestUsableMatchSlot) * 8, fast_adaptive_huffman_updating, use_polar_codes)) return false;
      if (!m_dist_lsb_table.init(true, 16, fast_adaptive_huffman_updating, use_polar_codes)) return false;

      for (uint i = 0; i < (1 << CLZBase::cNumLitPredBits); i++)
      {
         if (!m_lit_table[i].init(true, 256, fast_adaptive_huffman_updating, use_polar_codes)) return false;
      }

      for (uint i = 0; i < (1 << CLZBase::cNumDeltaLitPredBits); i++)
      {
         if (!m_delta_lit_table[i].init(true, 256, fast_adaptive_huffman_updating, use_polar_codes)) return false;
      }

      m_match_hist[0] = 1;
      m_match_hist[1] = 1;
      m_match_hist[2] = 1;
      m_match_hist[3] = 1;

      m_cur_ofs = 0;

      return true;
   }

   void lzcompressor::state_base::partial_advance(const lzdecision& lzdec)
   {
      if (lzdec.m_len == 0)
      {
         if (m_cur_state < 4) m_cur_state = 0; else if (m_cur_state < 10) m_cur_state -= 3; else m_cur_state -= 6;
      }
      else
      {
         if (lzdec.m_dist < 0)
         {
            int match_hist_index = -lzdec.m_dist - 1;

            if (!match_hist_index)
            {
               if (lzdec.m_len == 1)
               {
                  m_cur_state = (m_cur_state < CLZBase::cNumLitStates) ? 9 : 11;
               }
               else
               {
                  m_cur_state = (m_cur_state < CLZBase::cNumLitStates) ? 8 : 11;
               }
            }
            else
            {
               if (match_hist_index == 1)
               {
                  std::swap(m_match_hist[0], m_match_hist[1]);
               }
               else if (match_hist_index == 2)
               {
                  int dist = m_match_hist[2];
                  m_match_hist[2] = m_match_hist[1];
                  m_match_hist[1] = m_match_hist[0];
                  m_match_hist[0] = dist;
               }
               else
               {
                  LZHAM_ASSERT(match_hist_index == 3);

                  int dist = m_match_hist[3];
                  m_match_hist[3] = m_match_hist[2];
                  m_match_hist[2] = m_match_hist[1];
                  m_match_hist[1] = m_match_hist[0];
                  m_match_hist[0] = dist;
               }

               m_cur_state = (m_cur_state < CLZBase::cNumLitStates) ? 8 : 11;
            }
         }
         else
         {
            // full
            LZHAM_ASSUME(CLZBase::cMatchHistSize == 4);
            m_match_hist[3] = m_match_hist[2];
            m_match_hist[2] = m_match_hist[1];
            m_match_hist[1] = m_match_hist[0];
            m_match_hist[0] = lzdec.m_dist;

            m_cur_state = (m_cur_state < CLZBase::cNumLitStates) ? CLZBase::cNumLitStates : CLZBase::cNumLitStates + 3;
         }
      }

      m_cur_ofs = lzdec.m_pos + lzdec.get_len();
   }

   uint lzcompressor::state::get_pred_char(const search_accelerator& dict, int pos, int backward_ofs) const
   {
      LZHAM_ASSERT(pos >= (int)m_block_start_dict_ofs);
      int limit = pos - m_block_start_dict_ofs;
      if (backward_ofs > limit)
         return 0;
      return dict[pos - backward_ofs];
   }

   bit_cost_t lzcompressor::state::get_cost(CLZBase& lzbase, const search_accelerator& dict, const lzdecision& lzdec) const
   {
      const uint lit_pred0 = get_pred_char(dict, lzdec.m_pos, 1);

      uint is_match_model_index = LZHAM_COMPUTE_IS_MATCH_MODEL_INDEX(lit_pred0, m_cur_state);
      LZHAM_ASSERT(is_match_model_index < LZHAM_ARRAY_SIZE(m_is_match_model));
      bit_cost_t cost = m_is_match_model[is_match_model_index].get_cost(lzdec.is_match());

      if (!lzdec.is_match())
      {
         const uint lit = dict[lzdec.m_pos];

         if (m_cur_state < CLZBase::cNumLitStates)
         {
            const uint lit_pred1 = get_pred_char(dict, lzdec.m_pos, 2);

            uint lit_pred = (lit_pred0 >> (8 - CLZBase::cNumLitPredBits/2)) |
                            (((lit_pred1 >> (8 - CLZBase::cNumLitPredBits/2)) << CLZBase::cNumLitPredBits/2));

            // literal
            cost += m_lit_table[lit_pred].get_cost(lit);
         }
         else
         {
            // delta literal
            const uint rep_lit0 = dict[(lzdec.m_pos - m_match_hist[0]) & dict.m_max_dict_size_mask];
            const uint rep_lit1 = dict[(lzdec.m_pos - m_match_hist[0] - 1) & dict.m_max_dict_size_mask];

            uint delta_lit = rep_lit0 ^ lit;

            uint lit_pred = (rep_lit0 >> (8 - CLZBase::cNumDeltaLitPredBits/2)) |
                            ((rep_lit1 >> (8 - CLZBase::cNumDeltaLitPredBits/2)) << CLZBase::cNumDeltaLitPredBits/2);

            cost += m_delta_lit_table[lit_pred].get_cost(delta_lit);
         }
      }
      else
      {
         // match
         if (lzdec.m_dist < 0)
         {
            // rep match
            cost += m_is_rep_model[m_cur_state].get_cost(1);

            int match_hist_index = -lzdec.m_dist - 1;

            if (!match_hist_index)
            {
               // rep0 match
               cost += m_is_rep0_model[m_cur_state].get_cost(1);

               if (lzdec.m_len == 1)
               {
                  // single byte rep0
                  cost += m_is_rep0_single_byte_model[m_cur_state].get_cost(1);
               }
               else
               {
                  // normal rep0
                  cost += m_is_rep0_single_byte_model[m_cur_state].get_cost(0);

                  cost += m_rep_len_table[m_cur_state >= CLZBase::cNumLitStates].get_cost(lzdec.m_len - cMinMatchLen);
               }
            }
            else
            {
               cost += m_rep_len_table[m_cur_state >= CLZBase::cNumLitStates].get_cost(lzdec.m_len - cMinMatchLen);

               // rep1-rep3 match
               cost += m_is_rep0_model[m_cur_state].get_cost(0);

               if (match_hist_index == 1)
               {
                  // rep1
                  cost += m_is_rep1_model[m_cur_state].get_cost(1);
               }
               else
               {
                  cost += m_is_rep1_model[m_cur_state].get_cost(0);

                  if (match_hist_index == 2)
                  {
                     // rep2
                     cost += m_is_rep2_model[m_cur_state].get_cost(1);
                  }
                  else
                  {
                     LZHAM_ASSERT(match_hist_index == 3);
                     // rep3
                     cost += m_is_rep2_model[m_cur_state].get_cost(0);
                  }
               }
            }
         }
         else
         {
            cost += m_is_rep_model[m_cur_state].get_cost(0);

            LZHAM_ASSERT(lzdec.m_len >= cMinMatchLen);

            // full match
            uint match_slot, match_extra;
            lzbase.compute_lzx_position_slot(lzdec.m_dist, match_slot, match_extra);

            uint match_low_sym = 0;
            if (lzdec.m_len >= 9)
            {
               match_low_sym = 7;
               cost += m_large_len_table[m_cur_state >= CLZBase::cNumLitStates].get_cost(lzdec.m_len - 9);
            }
            else
               match_low_sym = lzdec.m_len - 2;

            uint match_high_sym = 0;

            LZHAM_ASSERT(match_slot >= CLZBase::cLZXLowestUsableMatchSlot && (match_slot < lzbase.m_num_lzx_slots));
            match_high_sym = match_slot - CLZBase::cLZXLowestUsableMatchSlot;

            uint main_sym = match_low_sym | (match_high_sym << 3);

            cost += m_main_table.get_cost(CLZBase::cLZXNumSpecialLengths + main_sym);

            uint num_extra_bits = lzbase.m_lzx_position_extra_bits[match_slot];
            if (num_extra_bits < 3)
               cost += (num_extra_bits << cBitCostScaleShift);
            else
            {
               if (num_extra_bits > 4)
                  cost += ((num_extra_bits - 4) << cBitCostScaleShift);

               cost += m_dist_lsb_table.get_cost(match_extra & 15);
            }
         }
      }

      return cost;
   }

   bit_cost_t lzcompressor::state::get_len2_match_cost(CLZBase& lzbase, uint dict_pos, uint len2_match_dist, uint is_match_model_index)
   {
      dict_pos;

      bit_cost_t cost = m_is_match_model[is_match_model_index].get_cost(1);

      cost += m_is_rep_model[m_cur_state].get_cost(0);

      // full match
      uint match_slot, match_extra;
      lzbase.compute_lzx_position_slot(len2_match_dist, match_slot, match_extra);

      const uint match_len = 2;
      uint match_low_sym = match_len - 2;

      uint match_high_sym = 0;

      LZHAM_ASSERT(match_slot >= CLZBase::cLZXLowestUsableMatchSlot && (match_slot < lzbase.m_num_lzx_slots));
      match_high_sym = match_slot - CLZBase::cLZXLowestUsableMatchSlot;

      uint main_sym = match_low_sym | (match_high_sym << 3);

      cost += m_main_table.get_cost(CLZBase::cLZXNumSpecialLengths + main_sym);

      uint num_extra_bits = lzbase.m_lzx_position_extra_bits[match_slot];
      if (num_extra_bits < 3)
         cost += (num_extra_bits << cBitCostScaleShift);
      else
      {
         if (num_extra_bits > 4)
            cost += ((num_extra_bits - 4) << cBitCostScaleShift);

         cost += m_dist_lsb_table.get_cost(match_extra & 15);
      }

      return cost;
   }

   bit_cost_t lzcompressor::state::get_lit_cost(const search_accelerator& dict, uint dict_pos, uint lit_pred0, uint is_match_model_index) const
   {
      bit_cost_t cost = m_is_match_model[is_match_model_index].get_cost(0);

      const uint lit = dict[dict_pos];

      if (m_cur_state < CLZBase::cNumLitStates)
      {
         // literal
         const uint lit_pred1 = get_pred_char(dict, dict_pos, 2);

         uint lit_pred = (lit_pred0 >> (8 - CLZBase::cNumLitPredBits/2)) |
            (((lit_pred1 >> (8 - CLZBase::cNumLitPredBits/2)) << CLZBase::cNumLitPredBits/2));

         cost += m_lit_table[lit_pred].get_cost(lit);
      }
      else
      {
         // delta literal
         const uint rep_lit0 = dict[(dict_pos - m_match_hist[0]) & dict.m_max_dict_size_mask];
         const uint rep_lit1 = dict[(dict_pos - m_match_hist[0] - 1) & dict.m_max_dict_size_mask];

         uint delta_lit = rep_lit0 ^ lit;

         uint lit_pred = (rep_lit0 >> (8 - CLZBase::cNumDeltaLitPredBits/2)) |
            ((rep_lit1 >> (8 - CLZBase::cNumDeltaLitPredBits/2)) << CLZBase::cNumDeltaLitPredBits/2);

         cost += m_delta_lit_table[lit_pred].get_cost(delta_lit);
      }

      return cost;
   }

   void lzcompressor::state::get_rep_match_costs(uint dict_pos, bit_cost_t *pBitcosts, uint match_hist_index, int min_len, int max_len, uint is_match_model_index) const
   {
      dict_pos;
      // match
      const sym_data_model &rep_len_table = m_rep_len_table[m_cur_state >= CLZBase::cNumLitStates];

      bit_cost_t base_cost = m_is_match_model[is_match_model_index].get_cost(1);

      base_cost += m_is_rep_model[m_cur_state].get_cost(1);

      if (!match_hist_index)
      {
         // rep0 match
         base_cost += m_is_rep0_model[m_cur_state].get_cost(1);
      }
      else
      {
         // rep1-rep3 matches
         base_cost += m_is_rep0_model[m_cur_state].get_cost(0);

         if (match_hist_index == 1)
         {
            // rep1
            base_cost += m_is_rep1_model[m_cur_state].get_cost(1);
         }
         else
         {
            base_cost += m_is_rep1_model[m_cur_state].get_cost(0);

            if (match_hist_index == 2)
            {
               // rep2
               base_cost += m_is_rep2_model[m_cur_state].get_cost(1);
            }
            else
            {
               // rep3
               base_cost += m_is_rep2_model[m_cur_state].get_cost(0);
            }
         }
      }

      // rep match
      if (!match_hist_index)
      {
         if (min_len == 1)
         {
            // single byte rep0
            pBitcosts[1] = base_cost + m_is_rep0_single_byte_model[m_cur_state].get_cost(1);
            min_len++;
         }

         bit_cost_t rep0_match_base_cost = base_cost + m_is_rep0_single_byte_model[m_cur_state].get_cost(0);
         for (int match_len = min_len; match_len <= max_len; match_len++)
         {
            // normal rep0
            pBitcosts[match_len] = rep0_match_base_cost + rep_len_table.get_cost(match_len - cMinMatchLen);
         }
      }
      else
      {
         for (int match_len = min_len; match_len <= max_len; match_len++)
         {
            pBitcosts[match_len] = base_cost + rep_len_table.get_cost(match_len - cMinMatchLen);
         }
      }
   }

   void lzcompressor::state::get_full_match_costs(CLZBase& lzbase, uint dict_pos, bit_cost_t *pBitcosts, uint match_dist, int min_len, int max_len, uint is_match_model_index) const
   {
      dict_pos;
      LZHAM_ASSERT(min_len >= cMinMatchLen);

      bit_cost_t cost = m_is_match_model[is_match_model_index].get_cost(1);

      cost += m_is_rep_model[m_cur_state].get_cost(0);

      uint match_slot, match_extra;
      lzbase.compute_lzx_position_slot(match_dist, match_slot, match_extra);
      LZHAM_ASSERT(match_slot >= CLZBase::cLZXLowestUsableMatchSlot && (match_slot < lzbase.m_num_lzx_slots));

      uint num_extra_bits = lzbase.m_lzx_position_extra_bits[match_slot];

      if (num_extra_bits < 3)
         cost += (num_extra_bits << cBitCostScaleShift);
      else
      {
         if (num_extra_bits > 4)
            cost += ((num_extra_bits - 4) << cBitCostScaleShift);

         cost += m_dist_lsb_table.get_cost(match_extra & 15);
      }

      uint match_high_sym = match_slot - CLZBase::cLZXLowestUsableMatchSlot;

      const sym_data_model &large_len_table = m_large_len_table[m_cur_state >= CLZBase::cNumLitStates];

      for (int match_len = min_len; match_len <= max_len; match_len++)
      {
         bit_cost_t len_cost = cost;

         uint match_low_sym = 0;
         if (match_len >= 9)
         {
            match_low_sym = 7;
            len_cost += large_len_table.get_cost(match_len - 9);
         }
         else
            match_low_sym = match_len - 2;

         uint main_sym = match_low_sym | (match_high_sym << 3);

         pBitcosts[match_len] = len_cost + m_main_table.get_cost(CLZBase::cLZXNumSpecialLengths + main_sym);
      }
   }


   bool lzcompressor::state::encode(symbol_codec& codec, CLZBase& lzbase, const search_accelerator& dict, const lzdecision& lzdec)
   {
      const uint lit_pred0 = get_pred_char(dict, lzdec.m_pos, 1);

      uint is_match_model_index = LZHAM_COMPUTE_IS_MATCH_MODEL_INDEX(lit_pred0, m_cur_state);
      if (!codec.encode(lzdec.is_match(), m_is_match_model[is_match_model_index])) return false;

      if (!lzdec.is_match())
      {
         const uint lit = dict[lzdec.m_pos];

#ifdef LZHAM_LZDEBUG
         if (!codec.encode_bits(lit, 8)) return false;
#endif

         if (m_cur_state < CLZBase::cNumLitStates)
         {
            const uint lit_pred1 = get_pred_char(dict, lzdec.m_pos, 2);

            uint lit_pred = (lit_pred0 >> (8 - CLZBase::cNumLitPredBits/2)) |
               (((lit_pred1 >> (8 - CLZBase::cNumLitPredBits/2)) << CLZBase::cNumLitPredBits/2));

            // literal
            if (!codec.encode(lit, m_lit_table[lit_pred])) return false;
         }
         else
         {
            // delta literal
            const uint rep_lit0 = dict[(lzdec.m_pos - m_match_hist[0]) & dict.m_max_dict_size_mask];
            const uint rep_lit1 = dict[(lzdec.m_pos - m_match_hist[0] - 1) & dict.m_max_dict_size_mask];

            uint delta_lit = rep_lit0 ^ lit;

            uint lit_pred = (rep_lit0 >> (8 - CLZBase::cNumDeltaLitPredBits/2)) |
                            ((rep_lit1 >> (8 - CLZBase::cNumDeltaLitPredBits/2)) << CLZBase::cNumDeltaLitPredBits/2);

#ifdef LZHAM_LZDEBUG
            if (!codec.encode_bits(rep_lit0, 8)) return false;
#endif

            if (!codec.encode(delta_lit, m_delta_lit_table[lit_pred])) return false;
         }

         if (m_cur_state < 4) m_cur_state = 0; else if (m_cur_state < 10) m_cur_state -= 3; else m_cur_state -= 6;
      }
      else
      {
         // match
         if (lzdec.m_dist < 0)
         {
            // rep match
            if (!codec.encode(1, m_is_rep_model[m_cur_state])) return false;

            int match_hist_index = -lzdec.m_dist - 1;

            if (!match_hist_index)
            {
               // rep0 match
               if (!codec.encode(1, m_is_rep0_model[m_cur_state])) return false;

               if (lzdec.m_len == 1)
               {
                  // single byte rep0
                  if (!codec.encode(1, m_is_rep0_single_byte_model[m_cur_state])) return false;

                  m_cur_state = (m_cur_state < CLZBase::cNumLitStates) ? 9 : 11;
               }
               else
               {
                  // normal rep0
                  if (!codec.encode(0, m_is_rep0_single_byte_model[m_cur_state])) return false;

                  if (!codec.encode(lzdec.m_len - cMinMatchLen, m_rep_len_table[m_cur_state >= CLZBase::cNumLitStates])) return false;

                  m_cur_state = (m_cur_state < CLZBase::cNumLitStates) ? 8 : 11;
               }
            }
            else
            {
               // rep1-rep3 match
               if (!codec.encode(0, m_is_rep0_model[m_cur_state])) return false;

               if (!codec.encode(lzdec.m_len - cMinMatchLen, m_rep_len_table[m_cur_state >= CLZBase::cNumLitStates])) return false;

               if (match_hist_index == 1)
               {
                  // rep1
                  if (!codec.encode(1, m_is_rep1_model[m_cur_state])) return false;

                  std::swap(m_match_hist[0], m_match_hist[1]);
               }
               else
               {
                  if (!codec.encode(0, m_is_rep1_model[m_cur_state])) return false;

                  if (match_hist_index == 2)
                  {
                     // rep2
                     if (!codec.encode(1, m_is_rep2_model[m_cur_state])) return false;

                     int dist = m_match_hist[2];
                     m_match_hist[2] = m_match_hist[1];
                     m_match_hist[1] = m_match_hist[0];
                     m_match_hist[0] = dist;
                  }
                  else
                  {
                     // rep3
                     if (!codec.encode(0, m_is_rep2_model[m_cur_state])) return false;

                     int dist = m_match_hist[3];
                     m_match_hist[3] = m_match_hist[2];
                     m_match_hist[2] = m_match_hist[1];
                     m_match_hist[1] = m_match_hist[0];
                     m_match_hist[0] = dist;
                  }
               }

               m_cur_state = (m_cur_state < CLZBase::cNumLitStates) ? 8 : 11;
            }
         }
         else
         {
            if (!codec.encode(0, m_is_rep_model[m_cur_state])) return false;

            LZHAM_ASSERT(lzdec.m_len >= cMinMatchLen);

            // full match
            uint match_slot, match_extra;
            lzbase.compute_lzx_position_slot(lzdec.m_dist, match_slot, match_extra);

            uint match_low_sym = 0;
            int large_len_sym = -1;
            if (lzdec.m_len >= 9)
            {
               match_low_sym = 7;

               large_len_sym = lzdec.m_len - 9;
            }
            else
               match_low_sym = lzdec.m_len - 2;

            uint match_high_sym = 0;

            LZHAM_ASSERT(match_slot >= CLZBase::cLZXLowestUsableMatchSlot && (match_slot < lzbase.m_num_lzx_slots));
            match_high_sym = match_slot - CLZBase::cLZXLowestUsableMatchSlot;

            uint main_sym = match_low_sym | (match_high_sym << 3);

            if (!codec.encode(CLZBase::cLZXNumSpecialLengths + main_sym, m_main_table)) return false;

            if (large_len_sym >= 0)
            {
               if (!codec.encode(large_len_sym, m_large_len_table[m_cur_state >= CLZBase::cNumLitStates])) return false;
            }

            uint num_extra_bits = lzbase.m_lzx_position_extra_bits[match_slot];
            if (num_extra_bits < 3)
            {
               if (!codec.encode_bits(match_extra, num_extra_bits)) return false;
            }
            else
            {
               if (num_extra_bits > 4)
               {
                  if (!codec.encode_bits((match_extra >> 4), num_extra_bits - 4)) return false;
               }

               if (!codec.encode(match_extra & 15, m_dist_lsb_table)) return false;
            }

            update_match_hist(lzdec.m_dist);

            m_cur_state = (m_cur_state < CLZBase::cNumLitStates) ? CLZBase::cNumLitStates : CLZBase::cNumLitStates + 3;
         }

#ifdef LZHAM_LZDEBUG
         if (!codec.encode_bits(m_match_hist[0], 29)) return false;
#endif
      }

      m_cur_ofs = lzdec.m_pos + lzdec.get_len();
      return true;
   }

   void lzcompressor::state::print(symbol_codec& codec, CLZBase& lzbase, const search_accelerator& dict, const lzdecision& lzdec)
   {
      codec, lzbase, dict;

      const uint lit_pred0 = get_pred_char(dict, lzdec.m_pos, 1);

      uint is_match_model_index = LZHAM_COMPUTE_IS_MATCH_MODEL_INDEX(lit_pred0, m_cur_state);

      printf("Pos: %u, state: %u, match_pred: %u, is_match_model_index: %u, is_match: %u, cost: %f\n",
         lzdec.m_pos,
         m_cur_state,
         lit_pred0, is_match_model_index, lzdec.is_match(), get_cost(lzbase, dict, lzdec) / (float)cBitCostScale);

      if (!lzdec.is_match())
      {
         const uint lit = dict[lzdec.m_pos];

         if (m_cur_state < CLZBase::cNumLitStates)
         {
            const uint lit_pred1 = get_pred_char(dict, lzdec.m_pos, 2);

            uint lit_pred = (lit_pred0 >> (8 - CLZBase::cNumLitPredBits/2)) |
               (((lit_pred1 >> (8 - CLZBase::cNumLitPredBits/2)) << CLZBase::cNumLitPredBits/2));

            printf("  ---Regular lit: %u '%c', lit_pred: %u\n", lit, ((lit >= 32) && (lit <= 127)) ? lit : '.', lit_pred);
         }
         else
         {
            // delta literal
            const uint rep_lit0 = dict[(lzdec.m_pos - m_match_hist[0]) & dict.m_max_dict_size_mask];
            const uint rep_lit1 = dict[(lzdec.m_pos - m_match_hist[0] - 1) & dict.m_max_dict_size_mask];

            uint delta_lit = rep_lit0 ^ lit;

            uint lit_pred = (rep_lit0 >> (8 - CLZBase::cNumDeltaLitPredBits/2)) |
               ((rep_lit1 >> (8 - CLZBase::cNumDeltaLitPredBits/2)) << CLZBase::cNumDeltaLitPredBits/2);

            printf("  ***Delta lit: %u '%c', Delta: 0x%02X, lit_pred: %u\n", lit, ((lit >= 32) && (lit <= 127)) ? lit : '.', delta_lit, lit_pred);
         }
      }
      else
      {
         uint actual_match_len = dict.get_match_len(0, lzdec.get_match_dist(*this), CLZBase::cMaxMatchLen);
         LZHAM_ASSERT(actual_match_len >= lzdec.get_len());

         // match
         if (lzdec.m_dist < 0)
         {
            int match_hist_index = -lzdec.m_dist - 1;

            if (!match_hist_index)
            {
               if (lzdec.m_len == 1)
               {
                  printf("  !!!Rep 0 len1\n");
               }
               else
               {
                  printf("  !!!Rep 0 full len %u\n", lzdec.m_len);
               }
            }
            else
            {
               printf("  !!!Rep %u full len %u\n", match_hist_index, lzdec.m_len);
            }
         }
         else
         {
            LZHAM_ASSERT(lzdec.m_len >= cMinMatchLen);

            // full match
            uint match_slot, match_extra;
            lzbase.compute_lzx_position_slot(lzdec.m_dist, match_slot, match_extra);

            uint match_low_sym = 0;
            int large_len_sym = -1;
            if (lzdec.m_len >= 9)
            {
               match_low_sym = 7;

               large_len_sym = lzdec.m_len - 9;
            }
            else
               match_low_sym = lzdec.m_len - 2;

            uint match_high_sym = 0;

            LZHAM_ASSERT(match_slot >= CLZBase::cLZXLowestUsableMatchSlot && (match_slot < lzbase.m_num_lzx_slots));
            match_high_sym = match_slot - CLZBase::cLZXLowestUsableMatchSlot;

            //uint main_sym = match_low_sym | (match_high_sym << 3);

            uint num_extra_bits = lzbase.m_lzx_position_extra_bits[match_slot];
            printf("  ^^^Full match Len %u Dist %u, Slot %u, ExtraBits: %u", lzdec.m_len, lzdec.m_dist, match_slot, num_extra_bits);

            if (num_extra_bits < 3)
            {
            }
            else
            {
               printf(" (Low 4 bits: %u)", match_extra & 15);
            }
            printf("\n");
         }

         if (actual_match_len > lzdec.get_len())
         {
            printf("  TRUNCATED match, actual len is %u, shortened by %u\n", actual_match_len, actual_match_len - lzdec.get_len());
         }
      }
   }

   bool lzcompressor::state::encode_eob(symbol_codec& codec, const search_accelerator& dict)
   {
#ifdef LZHAM_LZDEBUG
      if (!codec.encode_bits(CLZBase::cLZHAMDebugSyncMarkerValue, CLZBase::cLZHAMDebugSyncMarkerBits)) return false;
      if (!codec.encode_bits(1, 1)) return false;
      if (!codec.encode_bits(0, 9)) return false;
      if (!codec.encode_bits(m_cur_state, 4)) return false;
#endif

      uint match_pred = dict.get_cur_dict_size() ? dict.get_char(-1) : 0;
      uint is_match_model_index = LZHAM_COMPUTE_IS_MATCH_MODEL_INDEX(match_pred, m_cur_state);
      if (!codec.encode(1, m_is_match_model[is_match_model_index])) return false;

      // full match
      if (!codec.encode(0, m_is_rep_model[m_cur_state])) return false;

      return codec.encode(CLZBase::cLZXSpecialCodeEndOfBlockCode, m_main_table);
   }

   bool lzcompressor::state::encode_reset_state_partial(symbol_codec& codec, const search_accelerator& dict)
   {
#ifdef LZHAM_LZDEBUG
      if (!codec.encode_bits(CLZBase::cLZHAMDebugSyncMarkerValue, CLZBase::cLZHAMDebugSyncMarkerBits)) return false;
      if (!codec.encode_bits(1, 1)) return false;
      if (!codec.encode_bits(0, 9)) return false;
      if (!codec.encode_bits(m_cur_state, 4)) return false;
#endif

      uint match_pred = dict.get_cur_dict_size() ? dict.get_char(-1) : 0;
      uint is_match_model_index = LZHAM_COMPUTE_IS_MATCH_MODEL_INDEX(match_pred, m_cur_state);
      if (!codec.encode(1, m_is_match_model[is_match_model_index])) return false;

      // full match
      if (!codec.encode(0, m_is_rep_model[m_cur_state])) return false;

      if (!codec.encode(CLZBase::cLZXSpecialCodePartialStateReset, m_main_table))
         return false;

      reset_state_partial();
      return true;
   }

   void lzcompressor::state::update_match_hist(uint match_dist)
   {
      LZHAM_ASSUME(CLZBase::cMatchHistSize == 4);
      m_match_hist[3] = m_match_hist[2];
      m_match_hist[2] = m_match_hist[1];
      m_match_hist[1] = m_match_hist[0];
      m_match_hist[0] = match_dist;
   }

   int lzcompressor::state::find_match_dist(uint match_dist) const
   {
      for (uint match_hist_index = 0; match_hist_index < CLZBase::cMatchHistSize; match_hist_index++)
         if (match_dist == m_match_hist[match_hist_index])
            return match_hist_index;

      return -1;
   }

   void lzcompressor::state::reset_state_partial()
   {
      LZHAM_ASSUME(CLZBase::cMatchHistSize == 4);
      m_match_hist[0] = 1;
      m_match_hist[1] = 1;
      m_match_hist[2] = 1;
      m_match_hist[3] = 1;
      m_cur_state = 0;
   }

   void lzcompressor::state::start_of_block(const search_accelerator& dict, uint cur_ofs, uint block_index)
   {
      dict, block_index;

      reset_state_partial();

      m_cur_ofs = cur_ofs;
      m_block_start_dict_ofs = cur_ofs;
   }

   void lzcompressor::coding_stats::clear()
   {
      m_total_bytes = 0;
      m_total_contexts = 0;
      m_total_match_bits_cost = 0;
      m_worst_match_bits_cost = 0;
      m_total_is_match0_bits_cost = 0;
      m_total_is_match1_bits_cost = 0;

      m_total_nonmatches = 0;
      m_total_matches = 0;
      m_total_cost = 0.0f;
      m_total_lits = 0;
      m_total_lit_cost = 0;
      m_worst_lit_cost = 0;
      m_total_delta_lits = 0;
      m_total_delta_lit_cost = 0;
      m_worst_delta_lit_cost = 0;
      m_total_rep0_len1_matches = 0;
      m_total_reps = 0;
      m_total_rep0_len1_cost = 0;
      m_worst_rep0_len1_cost = 0;
      utils::zero_object(m_total_rep_matches);
      utils::zero_object(m_total_rep_cost);
      utils::zero_object(m_total_full_matches);
      utils::zero_object(m_total_full_match_cost);
      utils::zero_object(m_worst_rep_cost);
      utils::zero_object(m_worst_full_match_cost);
      m_total_far_len2_matches = 0;
      m_total_near_len2_matches = 0;
      m_total_is_rep0 = 0;
      m_total_is_rep0_len1 = 0;
      m_total_is_rep1 = 0;
      m_total_is_rep2 = 0;
      m_total_truncated_matches = 0;
      utils::zero_object(m_match_truncation_len_hist);
      utils::zero_object(m_match_truncation_hist);
      utils::zero_object(m_match_type_truncation_hist);
      utils::zero_object(m_match_type_was_not_truncated_hist);
   }

   void lzcompressor::coding_stats::print()
   {
      if (!m_total_contexts)
         return;

      printf("-----------\n");
      printf("Coding statistics:\n");
      printf("Total Bytes: %u, Total Contexts: %u, Total Cost: %f bits (%f bytes), Ave context cost: %f\n", m_total_bytes, m_total_contexts, m_total_cost, m_total_cost / 8.0f, m_total_cost / m_total_contexts);
      printf("Ave bytes per context: %f\n", m_total_bytes / (float)m_total_contexts);

      printf("IsMatch:\n");
      printf("  Total: %u, Cost: %f (%f bytes), Ave. Cost: %f, Worst Cost: %f\n",
         m_total_contexts, m_total_match_bits_cost, m_total_match_bits_cost / 8.0f, m_total_match_bits_cost / math::maximum<uint>(1, m_total_contexts), m_worst_match_bits_cost);

      printf("  IsMatch(0): %u, Cost: %f (%f bytes), Ave. Cost: %f\n",
         m_total_nonmatches, m_total_is_match0_bits_cost, m_total_is_match0_bits_cost / 8.0f, m_total_is_match0_bits_cost / math::maximum<uint>(1, m_total_nonmatches));

      printf("  IsMatch(1): %u, Cost: %f (%f bytes), Ave. Cost: %f\n",
         m_total_matches, m_total_is_match1_bits_cost, m_total_is_match1_bits_cost / 8.0f, m_total_is_match1_bits_cost / math::maximum<uint>(1, m_total_matches));

      printf("Literal stats:\n");
      printf("  Total: %u, Cost: %f (%f bytes), Ave. Cost: %f, Worst Cost: %f\n", m_total_lits, m_total_lit_cost, m_total_lit_cost / 8.0f, m_total_lit_cost / math::maximum<uint>(1, m_total_lits), m_worst_lit_cost);

      printf("Delta literal stats:\n");
      printf("  Total: %u, Cost: %f (%f bytes), Ave. Cost: %f, Worst Cost: %f\n", m_total_delta_lits, m_total_delta_lit_cost, m_total_delta_lit_cost / 8.0f, m_total_delta_lit_cost / math::maximum<uint>(1, m_total_delta_lits), m_worst_delta_lit_cost);

      printf("Rep0 Len1 stats:\n");
      printf("  Total: %u, Cost: %f (%f bytes), Ave. Cost: %f, Worst Cost: %f\n", m_total_rep0_len1_matches, m_total_rep0_len1_cost, m_total_rep0_len1_cost / 8.0f, m_total_rep0_len1_cost / math::maximum<uint>(1, m_total_rep0_len1_matches), m_worst_rep0_len1_cost);

      printf("Total IsRep0: %u IsRep1: %u IsRep2: %u\n", m_total_is_rep0, m_total_is_rep1, m_total_is_rep2);

      for (uint i = 0; i < CLZBase::cMatchHistSize; i++)
      {
         printf("Rep %u stats:\n", i);
         printf("  Total: %u, Cost: %f (%f bytes), Ave. Cost: %f, Worst Cost: %f\n", m_total_rep_matches[i], m_total_rep_cost[i], m_total_rep_cost[i] / 8.0f, m_total_rep_cost[i] / math::maximum<uint>(1, m_total_rep_matches[i]), m_worst_rep_cost[i]);
      }

      for (uint i = CLZBase::cMinMatchLen; i <= CLZBase::cMaxMatchLen; i++)
      {
         printf("Match %u: Total: %u, Cost: %f (%f bytes), Ave. Cost: %f, Worst Cost: %f\n", i,
            m_total_full_matches[i], m_total_full_match_cost[i], m_total_full_match_cost[i] / 8.0f, m_total_full_match_cost[i] / math::maximum<uint>(1, m_total_full_matches[i]), m_worst_full_match_cost[i]);
      }

      printf("Total near len2 matches: %u, total far len2 matches: %u\n", m_total_near_len2_matches, m_total_far_len2_matches);
      printf("Total matches: %u, truncated matches: %u\n", m_total_matches, m_total_truncated_matches);

      printf("Size of truncation histogram:\n");
      for (uint i = 0; i <= CLZBase::cMaxMatchLen; i++)
      {
         printf("%05u ", m_match_truncation_len_hist[i]);
         if ((i & 15) == 15) printf("\n");
      }
      printf("\n");

      printf("Number of truncations per encoded match length histogram:\n");
      for (uint i = 0; i <= CLZBase::cMaxMatchLen; i++)
      {
         printf("%05u ", m_match_truncation_hist[i]);
         if ((i & 15) == 15) printf("\n");
      }
      printf("\n");

      for (uint s = 0; s < CLZBase::cNumStates; s++)
      {
         printf("-- Match type truncation hist for state %u:\n", s);
         for (uint i = 0; i < LZHAM_ARRAY_SIZE(m_match_type_truncation_hist[s]); i++)
         {
            printf("%u truncated (%3.1f%%), %u not truncated\n", m_match_type_truncation_hist[s][i], 100.0f * (float)m_match_type_truncation_hist[s][i] / (m_match_type_truncation_hist[s][i] + m_match_type_was_not_truncated_hist[s][i]), m_match_type_was_not_truncated_hist[s][i]);
         }
      }
   }

   void lzcompressor::coding_stats::update(const lzdecision& lzdec, const state& cur_state, const search_accelerator& dict, bit_cost_t cost)
   {
      m_total_bytes += lzdec.get_len();
      m_total_contexts++;

      float cost_in_bits = cost / (float)cBitCostScale;

      m_total_cost += cost_in_bits;

      uint match_pred = cur_state.get_pred_char(dict, lzdec.m_pos, 1);
      uint is_match_model_index = LZHAM_COMPUTE_IS_MATCH_MODEL_INDEX(match_pred, cur_state.m_cur_state);

      if (lzdec.m_len == 0)
      {
         bit_cost_t match_bit_cost = cur_state.m_is_match_model[is_match_model_index].get_cost(0);
         m_total_is_match0_bits_cost += match_bit_cost;
         m_total_match_bits_cost += match_bit_cost;
         m_worst_match_bits_cost = math::maximum<double>(m_worst_match_bits_cost, static_cast<double>(match_bit_cost));
         m_total_nonmatches++;

         if (cur_state.m_cur_state < CLZBase::cNumLitStates)
         {
            m_total_lits++;
            m_total_lit_cost += cost_in_bits;
            m_worst_lit_cost = math::maximum<double>(m_worst_lit_cost, cost_in_bits);
         }
         else
         {
            m_total_delta_lits++;
            m_total_delta_lit_cost += cost_in_bits;
            m_worst_delta_lit_cost = math::maximum<double>(m_worst_delta_lit_cost, cost_in_bits);
         }
      }
      else
      {
         {
            uint match_dist = lzdec.get_match_dist(cur_state);

            uint actual_match_len = dict.get_match_len(0, match_dist, CLZBase::cMaxMatchLen);
            LZHAM_VERIFY(lzdec.get_len() <= actual_match_len);

            m_total_truncated_matches += lzdec.get_len() < actual_match_len;
            m_match_truncation_len_hist[actual_match_len - lzdec.get_len()]++;

            uint type_index = 4;
            if (!lzdec.is_full_match())
            {
               LZHAM_ASSUME(CLZBase::cMatchHistSize == 4);
               type_index = -lzdec.m_dist - 1;
            }

            if (actual_match_len > lzdec.get_len())
            {
               m_match_truncation_hist[lzdec.get_len()]++;

               m_match_type_truncation_hist[cur_state.m_cur_state][type_index]++;
            }
            else
            {
               m_match_type_was_not_truncated_hist[cur_state.m_cur_state][type_index]++;
            }
         }

         bit_cost_t match_bit_cost = cur_state.m_is_match_model[is_match_model_index].get_cost(1);
         m_total_is_match1_bits_cost += match_bit_cost;
         m_total_match_bits_cost += match_bit_cost;
         m_worst_match_bits_cost = math::maximum<double>(m_worst_match_bits_cost, static_cast<double>(match_bit_cost));
         m_total_matches++;

         if (lzdec.m_dist < 0)
         {
            m_total_reps++;
            m_total_is_rep0++;

            // rep match

            int match_hist_index = -lzdec.m_dist - 1;

            if (!match_hist_index)
            {
               m_total_is_rep0_len1++;

               // rep0 match
               if (lzdec.m_len == 1)
               {
                  m_total_rep0_len1_matches++;
                  m_total_rep0_len1_cost += cost_in_bits;
                  m_worst_rep0_len1_cost = math::maximum<double>(m_worst_rep0_len1_cost, cost_in_bits);
               }
               else
               {
                  m_total_rep_matches[0]++;
                  m_total_rep_cost[0] += cost_in_bits;
                  m_worst_rep_cost[0] = math::maximum<double>(m_worst_rep_cost[0], cost_in_bits);
               }
            }
            else
            {
               m_total_is_rep1++;

               if (match_hist_index > 1)
               {
                  m_total_is_rep2++;
               }

               LZHAM_ASSERT(match_hist_index < CLZBase::cMatchHistSize);
               m_total_rep_matches[match_hist_index]++;
               m_total_rep_cost[match_hist_index] += cost_in_bits;
               m_worst_rep_cost[match_hist_index] = math::maximum<double>(m_worst_rep_cost[match_hist_index], cost_in_bits);
            }
         }
         else
         {
            m_total_full_matches[lzdec.get_len()]++;
            m_total_full_match_cost[lzdec.get_len()] += cost_in_bits;
            m_worst_full_match_cost[lzdec.get_len()] = math::maximum<double>(m_worst_full_match_cost[lzdec.get_len()], cost_in_bits);

            if (lzdec.get_len() == 2)
            {
               if (lzdec.m_dist <= 512)
                  m_total_near_len2_matches++;
               else
                  m_total_far_len2_matches++;
            }
         }
      }
   }

   lzcompressor::lzcompressor() :
      m_src_size(-1),
      m_src_adler32(0),
      m_step(0),
      m_block_start_dict_ofs(0),
      m_block_index(0),
      m_finished(false),
      m_num_parse_threads(0),
      m_parse_jobs_remaining(0)
   {
      LZHAM_VERIFY( ((uint32_ptr)this & (LZHAM_GET_ALIGNMENT(lzcompressor) - 1)) == 0);
   }

   bool lzcompressor::init(const init_params& params)
   {
      clear();

      if ((params.m_dict_size_log2 < CLZBase::cMinDictSizeLog2) || (params.m_dict_size_log2 > CLZBase::cMaxDictSizeLog2))
         return false;
      if ((params.m_compression_level < 0) || (params.m_compression_level > cCompressionLevelCount))
         return false;

      m_params = params;
      m_use_task_pool = (m_params.m_pTask_pool) && (m_params.m_pTask_pool->get_num_threads() != 0) && (m_params.m_max_helper_threads > 0);
      if ((m_params.m_max_helper_threads) && (!m_use_task_pool))
         return false;
      m_settings = s_settings[params.m_compression_level];

      if (m_params.m_lzham_compress_flags & LZHAM_COMP_FLAG_FORCE_POLAR_CODING)
         m_settings.m_use_polar_codes = true;

      const uint dict_size = 1U << m_params.m_dict_size_log2;

      uint max_block_size = dict_size / 8;
      if (m_params.m_block_size > max_block_size)
      {
         m_params.m_block_size = max_block_size;
      }

      m_num_parse_threads = 1;

#if !LZHAM_FORCE_SINGLE_THREADED_PARSING
      if (params.m_max_helper_threads > 0)
      {
         LZHAM_ASSUME(cMaxParseThreads >= 4);

         if (m_params.m_block_size < 16384)
         {
            m_num_parse_threads = LZHAM_MIN(cMaxParseThreads, params.m_max_helper_threads + 1);
         }
         else
         {
            if ((params.m_max_helper_threads == 1) || (m_params.m_compression_level == cCompressionLevelFastest))
            {
               m_num_parse_threads = 1;
            }
            else if (params.m_max_helper_threads <= 3)
            {
               m_num_parse_threads = 2;
            }
            else if (params.m_max_helper_threads <= 7)
            {
               if ((m_params.m_lzham_compress_flags & LZHAM_COMP_FLAG_EXTREME_PARSING) && (m_params.m_compression_level == cCompressionLevelUber))
                  m_num_parse_threads = 4;
               else
                  m_num_parse_threads = 2;
            }
            else
            {
               // 8-16
               m_num_parse_threads = 4;
            }
         }
      }
#endif

      int num_parse_jobs = m_num_parse_threads - 1;
      uint match_accel_helper_threads = LZHAM_MAX(0, (int)params.m_max_helper_threads - num_parse_jobs);

      LZHAM_ASSERT(m_num_parse_threads >= 1);
      LZHAM_ASSERT(m_num_parse_threads <= cMaxParseThreads);

      if (!m_use_task_pool)
      {
         LZHAM_ASSERT(!match_accel_helper_threads && (m_num_parse_threads == 1));
      }
      else
      {
         LZHAM_ASSERT((match_accel_helper_threads + (m_num_parse_threads - 1)) <= params.m_max_helper_threads);
      }

      if (!m_accel.init(this, params.m_pTask_pool, match_accel_helper_threads, dict_size, m_settings.m_match_accel_max_matches_per_probe, false, m_settings.m_match_accel_max_probes))
         return false;

      init_position_slots(params.m_dict_size_log2);
      init_slot_tabs();

      if (!m_state.init(*this, m_settings.m_fast_adaptive_huffman_updating, m_settings.m_use_polar_codes))
         return false;

      if (!m_block_buf.try_reserve(m_params.m_block_size))
         return false;

      if (!m_comp_buf.try_reserve(m_params.m_block_size*2))
         return false;

      for (uint i = 0; i < m_num_parse_threads; i++)
      {
         if (!m_parse_thread_state[i].m_approx_state.init(*this, m_settings.m_fast_adaptive_huffman_updating, m_settings.m_use_polar_codes))
            return false;
      }

      return true;
   }

   void lzcompressor::clear()
   {
      m_codec.clear();
      m_src_size = 0;
      m_src_adler32 = cInitAdler32;
      m_block_buf.clear();
      m_comp_buf.clear();

      m_step = 0;
      m_finished = false;
      m_use_task_pool = false;
      m_block_start_dict_ofs = 0;
      m_block_index = 0;
      m_state.clear();
      m_num_parse_threads = 0;
      m_parse_jobs_remaining = 0;

      for (uint i = 0; i < cMaxParseThreads; i++)
      {
         parse_thread_state &parse_state = m_parse_thread_state[i];
         parse_state.m_approx_state.clear();

         for (uint j = 0; j <= cMaxParseGraphNodes; j++)
            parse_state.m_nodes[j].clear();

         parse_state.m_start_ofs = 0;
         parse_state.m_bytes_to_match = 0;
         parse_state.m_best_decisions.clear();
         parse_state.m_issued_reset_state_partial = false;
         parse_state.m_emit_decisions_backwards = false;
         parse_state.m_failed = false;
      }
   }

   bool lzcompressor::code_decision(lzdecision lzdec, uint& cur_ofs, uint& bytes_to_match)
   {
#ifdef LZHAM_LZDEBUG
      if (!m_codec.encode_bits(CLZBase::cLZHAMDebugSyncMarkerValue, CLZBase::cLZHAMDebugSyncMarkerBits)) return false;
      if (!m_codec.encode_bits(lzdec.is_match(), 1)) return false;
      if (!m_codec.encode_bits(lzdec.get_len(), 9)) return false;
      if (!m_codec.encode_bits(m_state.m_cur_state, 4)) return false;
#endif

#ifdef LZHAM_LZVERIFY
      if (lzdec.is_match())
      {
         uint match_dist = lzdec.get_match_dist(m_state);

         LZHAM_VERIFY(m_accel[cur_ofs] == m_accel[(cur_ofs - match_dist) & (m_accel.get_max_dict_size() - 1)]);
      }
#endif

      const uint len = lzdec.get_len();

      if (!m_state.encode(m_codec, *this, m_accel, lzdec))
         return false;

      cur_ofs += len;
      LZHAM_ASSERT(bytes_to_match >= len);
      bytes_to_match -= len;

      m_accel.advance_bytes(len);

      m_step++;

      return true;
   }

   bool lzcompressor::put_bytes(const void* pBuf, uint buf_len)
   {
      LZHAM_ASSERT(!m_finished);
      if (m_finished)
         return false;

      bool status = true;

      if (!pBuf)
      {
         if (m_block_buf.size())
         {
            status = compress_block(m_block_buf.get_ptr(), m_block_buf.size());

            m_block_buf.try_resize(0);
         }

         if (status)
         {
            if (!send_final_block())
            {
               status = false;
            }
         }

         m_finished = true;
      }
      else
      {
         const uint8 *pSrcBuf = static_cast<const uint8*>(pBuf);
         uint num_src_bytes_remaining = buf_len;

         while (num_src_bytes_remaining)
         {
            const uint num_bytes_to_copy = LZHAM_MIN(num_src_bytes_remaining, m_params.m_block_size - m_block_buf.size());

            if (num_bytes_to_copy == m_params.m_block_size)
            {
               LZHAM_ASSERT(!m_block_buf.size());

               status = compress_block(pSrcBuf, num_bytes_to_copy);
            }
            else
            {
               if (!m_block_buf.append(static_cast<const uint8 *>(pSrcBuf), num_bytes_to_copy)) return false;

               LZHAM_ASSERT(m_block_buf.size() <= m_params.m_block_size);

               if (m_block_buf.size() == m_params.m_block_size)
               {
                  status = compress_block(m_block_buf.get_ptr(), m_block_buf.size());

                  m_block_buf.try_resize(0);
               }
            }

            pSrcBuf += num_bytes_to_copy;
            num_src_bytes_remaining -= num_bytes_to_copy;
         }
      }

      lzham_flush_buffered_printf();

      return status;
   }

   bool lzcompressor::send_final_block()
   {
      //m_codec.clear();

      if (!m_codec.start_encoding(16))
         return false;

#ifdef LZHAM_LZDEBUG
      if (!m_codec.encode_bits(166, 12))
         return false;
#endif

      if (!m_block_index)
      {
         if (!send_configuration())
            return false;
      }

      if (!m_codec.encode_bits(cEOFBlock, cBlockHeaderBits))
         return false;

      if (!m_codec.encode_align_to_byte())
         return false;

      if (!m_codec.encode_bits(m_src_adler32, 32))
         return false;

      if (!m_codec.stop_encoding(true))
         return false;

      if (m_comp_buf.empty())
      {
         m_comp_buf.swap(m_codec.get_encoding_buf());
      }
      else
      {
         if (!m_comp_buf.append(m_codec.get_encoding_buf()))
            return false;
      }

      m_block_index++;

#if LZHAM_UPDATE_STATS
      m_stats.print();
#endif

      return true;
   }

   bool lzcompressor::send_configuration()
   {
      if (!m_codec.encode_bits(m_settings.m_fast_adaptive_huffman_updating, 1))
         return false;
      if (!m_codec.encode_bits(m_settings.m_use_polar_codes, 1))
         return false;

      return true;
   }

   // TODO: implement greedy_parse() (or flexible_parse?)
   bool lzcompressor::greedy_parse(parse_thread_state &parse_state)
   {
      parse_state;
      return false;
   }

   void lzcompressor::node::add_state(
      int parent_index, int parent_state_index,
      const lzdecision &lzdec, state &parent_state,
      bit_cost_t total_cost,
      uint total_complexity)
   {
      state_base trial_state;
      parent_state.save_partial_state(trial_state);
      trial_state.partial_advance(lzdec);

      for (int i = m_num_node_states - 1; i >= 0; i--)
      {
         node_state &cur_node_state = m_node_states[i];
         if (cur_node_state.m_saved_state == trial_state)
         {
            if ( (total_cost < cur_node_state.m_total_cost) ||
                 ((total_cost == cur_node_state.m_total_cost) && (total_complexity < cur_node_state.m_total_complexity)) )
            {
               cur_node_state.m_parent_index = static_cast<int16>(parent_index);
               cur_node_state.m_parent_state_index = static_cast<int8>(parent_state_index);
               cur_node_state.m_lzdec = lzdec;
               cur_node_state.m_total_cost = total_cost;
               cur_node_state.m_total_complexity = total_complexity;

               while (i > 0)
               {
                  if ((m_node_states[i].m_total_cost < m_node_states[i - 1].m_total_cost) ||
                      ((m_node_states[i].m_total_cost == m_node_states[i - 1].m_total_cost) && (m_node_states[i].m_total_complexity < m_node_states[i - 1].m_total_complexity)))
                  {
                     std::swap(m_node_states[i], m_node_states[i - 1]);
                     i--;
                  }
                  else
                     break;
               }
            }

            return;
         }
      }

      int insert_index;
      for (insert_index = m_num_node_states; insert_index > 0; insert_index--)
      {
         node_state &cur_node_state = m_node_states[insert_index - 1];

         if ( (total_cost > cur_node_state.m_total_cost) ||
              ((total_cost == cur_node_state.m_total_cost) && (total_complexity >= cur_node_state.m_total_complexity)) )
         {
            break;
         }
      }

      if (insert_index == cMaxNodeStates)
         return;

      uint num_behind = m_num_node_states - insert_index;
      uint num_to_move = (m_num_node_states < cMaxNodeStates) ? num_behind : (num_behind - 1);
      if (num_to_move)
      {
         LZHAM_ASSERT((insert_index + 1 + num_to_move) <= cMaxNodeStates);
         memmove( &m_node_states[insert_index + 1], &m_node_states[insert_index], sizeof(node_state) * num_to_move);
      }

      node_state *pNew_node_state = &m_node_states[insert_index];
      pNew_node_state->m_parent_index = static_cast<int16>(parent_index);
      pNew_node_state->m_parent_state_index = static_cast<uint8>(parent_state_index);
      pNew_node_state->m_lzdec = lzdec;
      pNew_node_state->m_total_cost = total_cost;
      pNew_node_state->m_total_complexity = total_complexity;
      pNew_node_state->m_saved_state = trial_state;

      m_num_node_states = LZHAM_MIN(m_num_node_states + 1, cMaxNodeStates);

#ifdef LZHAM_LZVERIFY
      for (uint i = 0; i < (m_num_node_states - 1); ++i)
      {
         node_state &a = m_node_states[i];
         node_state &b = m_node_states[i + 1];
         LZHAM_VERIFY(
            (a.m_total_cost < b.m_total_cost) ||
            ((a.m_total_cost == b.m_total_cost) && (a.m_total_complexity <= b.m_total_complexity)) );
      }
#endif
   }

   bool lzcompressor::extreme_parse(parse_thread_state &parse_state)
   {
      LZHAM_ASSERT(parse_state.m_bytes_to_match <= cMaxParseGraphNodes);

      parse_state.m_failed = false;
      parse_state.m_emit_decisions_backwards = true;

      node *pNodes = parse_state.m_nodes;
      for (uint i = 0; i <= cMaxParseGraphNodes; i++)
      {
         pNodes[i].clear();
      }

      state &approx_state = parse_state.m_approx_state;

      pNodes[0].m_num_node_states = 1;
      node_state &first_node_state = pNodes[0].m_node_states[0];
      approx_state.save_partial_state(first_node_state.m_saved_state);
      first_node_state.m_parent_index = -1;
      first_node_state.m_parent_state_index = -1;
      first_node_state.m_total_cost = 0;
      first_node_state.m_total_complexity = 0;

      const uint bytes_to_parse = parse_state.m_bytes_to_match;

      const uint lookahead_start_ofs = m_accel.get_lookahead_pos() & m_accel.get_max_dict_size_mask();

      uint cur_dict_ofs = parse_state.m_start_ofs;
      uint cur_lookahead_ofs = cur_dict_ofs - lookahead_start_ofs;
      uint cur_node_index = 0;

      enum { cMaxFullMatches = cMatchAccelMaxSupportedProbes };
      uint match_lens[cMaxFullMatches];
      uint match_distances[cMaxFullMatches];

      bit_cost_t lzdec_bitcosts[cMaxMatchLen + 1];

      node prev_lit_node;
      prev_lit_node.clear();

      while (cur_node_index < bytes_to_parse)
      {
         node* pCur_node = &pNodes[cur_node_index];

         const uint max_admissable_match_len = LZHAM_MIN(CLZBase::cMaxMatchLen, bytes_to_parse - cur_node_index);
         const uint find_dict_size = m_accel.get_cur_dict_size() + cur_lookahead_ofs;

         const uint lit_pred0 = approx_state.get_pred_char(m_accel, cur_dict_ofs, 1);

         const uint8* pLookahead = &m_accel.m_dict[cur_dict_ofs];

         // full matches
         uint max_full_match_len = 0;
         uint num_full_matches = 0;
         uint len2_match_dist = 0;

         if (max_admissable_match_len >= cMinMatchLen)
         {
            const dict_match* pMatches = m_accel.find_matches(cur_lookahead_ofs);
            if (pMatches)
            {
               for ( ; ; )
               {
                  uint match_len = pMatches->get_len();
                  LZHAM_ASSERT((pMatches->get_dist() > 0) && (pMatches->get_dist() <= m_dict_size));
                  match_len = LZHAM_MIN(match_len, max_admissable_match_len);

                  if (match_len > max_full_match_len)
                  {
                     max_full_match_len = match_len;

                     match_lens[num_full_matches] = match_len;
                     match_distances[num_full_matches] = pMatches->get_dist();
                     num_full_matches++;
                  }

                  if (pMatches->is_last())
                     break;
                  pMatches++;
               }
            }

            len2_match_dist = m_accel.get_len2_match(cur_lookahead_ofs);
         }

         for (uint cur_node_state_index = 0; cur_node_state_index < pCur_node->m_num_node_states; cur_node_state_index++)
         {
            node_state &cur_node_state = pCur_node->m_node_states[cur_node_state_index];

            if (cur_node_index)
            {
               LZHAM_ASSERT(cur_node_state.m_parent_index >= 0);

               approx_state.restore_partial_state(cur_node_state.m_saved_state);
            }

            uint is_match_model_index = LZHAM_COMPUTE_IS_MATCH_MODEL_INDEX(lit_pred0, approx_state.m_cur_state);

            const bit_cost_t cur_node_total_cost = cur_node_state.m_total_cost;
            const uint cur_node_total_complexity = cur_node_state.m_total_complexity;

            // rep matches
            uint match_hist_max_len = 0;
            uint match_hist_min_match_len = 1;
            for (uint rep_match_index = 0; rep_match_index < cMatchHistSize; rep_match_index++)
            {
               uint hist_match_len = 0;

               uint dist = approx_state.m_match_hist[rep_match_index];
               if (dist <= find_dict_size)
               {
                  const uint comp_pos = static_cast<uint>((m_accel.m_lookahead_pos + cur_lookahead_ofs - dist) & m_accel.m_max_dict_size_mask);
                  const uint8* pComp = &m_accel.m_dict[comp_pos];

                  for (hist_match_len = 0; hist_match_len < max_admissable_match_len; hist_match_len++)
                     if (pComp[hist_match_len] != pLookahead[hist_match_len])
                        break;
               }

               if (hist_match_len >= match_hist_min_match_len)
               {
                  match_hist_max_len = math::maximum(match_hist_max_len, hist_match_len);

                  approx_state.get_rep_match_costs(cur_dict_ofs, lzdec_bitcosts, rep_match_index, match_hist_min_match_len, hist_match_len, is_match_model_index);

                  uint rep_match_total_complexity = cur_node_total_complexity + (cRep0Complexity + rep_match_index);
                  for (uint l = match_hist_min_match_len; l <= hist_match_len; l++)
                  {
#if LZHAM_VERIFY_MATCH_COSTS
                     {
                        lzdecision actual_dec(cur_dict_ofs, l, -((int)rep_match_index + 1));
                        bit_cost_t actual_cost = approx_state.get_cost(*this, m_accel, actual_dec);
                        LZHAM_ASSERT(actual_cost == lzdec_bitcosts[l]);
                     }
#endif
                     node& dst_node = pCur_node[l];

                     bit_cost_t rep_match_total_cost = cur_node_total_cost + lzdec_bitcosts[l];

                     dst_node.add_state(cur_node_index, cur_node_state_index, lzdecision(cur_dict_ofs, l, -((int)rep_match_index + 1)), approx_state, rep_match_total_cost, rep_match_total_complexity);
                  }
               }

               match_hist_min_match_len = cMinMatchLen;
            }

            uint min_truncate_match_len = match_hist_max_len;

            // nearest len2 match
            if (len2_match_dist)
            {
               lzdecision lzdec(cur_dict_ofs, 2, len2_match_dist);
               bit_cost_t actual_cost = approx_state.get_cost(*this, m_accel, lzdec);
               pCur_node[2].add_state(cur_node_index, cur_node_state_index, lzdec, approx_state, cur_node_total_cost + actual_cost, cur_node_total_complexity + cShortMatchComplexity);

               min_truncate_match_len = LZHAM_MAX(min_truncate_match_len, 2);
            }

            // full matches
            if (max_full_match_len > min_truncate_match_len)
            {
               uint prev_max_match_len = LZHAM_MAX(1, min_truncate_match_len);
               for (uint full_match_index = 0; full_match_index < num_full_matches; full_match_index++)
               {
                  uint end_len = match_lens[full_match_index];
                  if (end_len <= min_truncate_match_len)
                     continue;

                  uint start_len = prev_max_match_len + 1;
                  uint match_dist = match_distances[full_match_index];

                  LZHAM_ASSERT(start_len <= end_len);

                  approx_state.get_full_match_costs(*this, cur_dict_ofs, lzdec_bitcosts, match_dist, start_len, end_len, is_match_model_index);

                  for (uint l = start_len; l <= end_len; l++)
                  {
                     uint match_complexity = (l >= cLongMatchComplexityLenThresh) ? cLongMatchComplexity : cShortMatchComplexity;

#if LZHAM_VERIFY_MATCH_COSTS
                     {
                        lzdecision actual_dec(cur_dict_ofs, l, match_dist);
                        bit_cost_t actual_cost = approx_state.get_cost(*this, m_accel, actual_dec);
                        LZHAM_ASSERT(actual_cost == lzdec_bitcosts[l]);
                     }
#endif
                     node& dst_node = pCur_node[l];

                     bit_cost_t match_total_cost = cur_node_total_cost + lzdec_bitcosts[l];
                     uint match_total_complexity = cur_node_total_complexity + match_complexity;

                     dst_node.add_state( cur_node_index, cur_node_state_index, lzdecision(cur_dict_ofs, l, match_dist), approx_state, match_total_cost, match_total_complexity);
                  }

                  prev_max_match_len = end_len;
               }
            }

            // literal
            bit_cost_t lit_cost = approx_state.get_lit_cost(m_accel, cur_dict_ofs, lit_pred0, is_match_model_index);
            bit_cost_t lit_total_cost = cur_node_total_cost + lit_cost;
            uint lit_total_complexity = cur_node_total_complexity + cLitComplexity;
#if LZHAM_VERIFY_MATCH_COSTS
            {
               lzdecision actual_dec(cur_dict_ofs, 0, 0);
               bit_cost_t actual_cost = approx_state.get_cost(*this, m_accel, actual_dec);
               LZHAM_ASSERT(actual_cost == lit_cost);
            }
#endif

            pCur_node[1].add_state( cur_node_index, cur_node_state_index, lzdecision(cur_dict_ofs, 0, 0), approx_state, lit_total_cost, lit_total_complexity);

         } // cur_node_state_index

         cur_dict_ofs++;
         cur_lookahead_ofs++;
         cur_node_index++;
      }

      // Now get the optimal decisions by starting from the goal node.
      // m_best_decisions is filled backwards.
      if (!parse_state.m_best_decisions.try_reserve(bytes_to_parse))
      {
         parse_state.m_failed = true;
         return false;
      }

      bit_cost_t lowest_final_cost = cBitCostMax; //math::cNearlyInfinite;
      int node_state_index = 0;
      node_state *pLast_node_states = pNodes[bytes_to_parse].m_node_states;
      for (uint i = 0; i < pNodes[bytes_to_parse].m_num_node_states; i++)
      {
         if (pLast_node_states[i].m_total_cost < lowest_final_cost)
         {
            lowest_final_cost = pLast_node_states[i].m_total_cost;
            node_state_index = i;
         }
      }

      int node_index = bytes_to_parse;
      lzdecision *pDst_dec = parse_state.m_best_decisions.get_ptr();
      do
      {
         LZHAM_ASSERT((node_index >= 0) && (node_index <= (int)cMaxParseGraphNodes));

         node& cur_node = pNodes[node_index];
         const node_state &cur_node_state = cur_node.m_node_states[node_state_index];

         *pDst_dec++ = cur_node_state.m_lzdec;

         node_index = cur_node_state.m_parent_index;
         node_state_index = cur_node_state.m_parent_state_index;

      } while (node_index > 0);

      parse_state.m_best_decisions.try_resize(static_cast<uint>(pDst_dec - parse_state.m_best_decisions.get_ptr()));

      return true;
   }

   bool lzcompressor::optimal_parse(parse_thread_state &parse_state)
   {
      LZHAM_ASSERT(parse_state.m_bytes_to_match <= cMaxParseGraphNodes);

      parse_state.m_failed = false;
      parse_state.m_emit_decisions_backwards = true;

      node_state *pNodes = reinterpret_cast<node_state*>(parse_state.m_nodes);
      pNodes[0].m_parent_index = -1;
      pNodes[0].m_total_cost = 0;
      pNodes[0].m_total_complexity = 0;

#if 0
      for (uint i = 1; i <= cMaxParseGraphNodes; i++)
      {
         pNodes[i].clear();
      }
#else
      memset( &pNodes[1], 0xFF, cMaxParseGraphNodes * sizeof(node_state));
#endif

      state &approx_state = parse_state.m_approx_state;

      const uint bytes_to_parse = parse_state.m_bytes_to_match;

      const uint lookahead_start_ofs = m_accel.get_lookahead_pos() & m_accel.get_max_dict_size_mask();

      uint cur_dict_ofs = parse_state.m_start_ofs;
      uint cur_lookahead_ofs = cur_dict_ofs - lookahead_start_ofs;
      uint cur_node_index = 0;

      enum { cMaxFullMatches = cMatchAccelMaxSupportedProbes };
      uint match_lens[cMaxFullMatches];
      uint match_distances[cMaxFullMatches];

      bit_cost_t lzdec_bitcosts[cMaxMatchLen + 1];

      while (cur_node_index < bytes_to_parse)
      {
         node_state* pCur_node = &pNodes[cur_node_index];

         const uint max_admissable_match_len = LZHAM_MIN(CLZBase::cMaxMatchLen, bytes_to_parse - cur_node_index);
         const uint find_dict_size = m_accel.m_cur_dict_size + cur_lookahead_ofs;

         if (cur_node_index)
         {
            LZHAM_ASSERT(pCur_node->m_parent_index >= 0);

            // Move to this node's state using the lowest cost LZ decision found.
            approx_state.restore_partial_state(pCur_node->m_saved_state);
            approx_state.partial_advance(pCur_node->m_lzdec);
         }

         const bit_cost_t cur_node_total_cost = pCur_node->m_total_cost;
         // This assert includes a fudge factor - make sure we don't overflow our scaled costs.
         LZHAM_ASSERT((cBitCostMax - cur_node_total_cost) > (cBitCostScale * 64));
         const uint cur_node_total_complexity = pCur_node->m_total_complexity;

         const uint lit_pred0 = approx_state.get_pred_char(m_accel, cur_dict_ofs, 1);
         uint is_match_model_index = LZHAM_COMPUTE_IS_MATCH_MODEL_INDEX(lit_pred0, approx_state.m_cur_state);

         const uint8* pLookahead = &m_accel.m_dict[cur_dict_ofs];

         // rep matches
         uint match_hist_max_len = 0;
         uint match_hist_min_match_len = 1;
         for (uint rep_match_index = 0; rep_match_index < cMatchHistSize; rep_match_index++)
         {
            uint hist_match_len = 0;

            uint dist = approx_state.m_match_hist[rep_match_index];
            if (dist <= find_dict_size)
            {
               const uint comp_pos = static_cast<uint>((m_accel.m_lookahead_pos + cur_lookahead_ofs - dist) & m_accel.m_max_dict_size_mask);
               const uint8* pComp = &m_accel.m_dict[comp_pos];

               for (hist_match_len = 0; hist_match_len < max_admissable_match_len; hist_match_len++)
                  if (pComp[hist_match_len] != pLookahead[hist_match_len])
                     break;
            }

            if (hist_match_len >= match_hist_min_match_len)
            {
               match_hist_max_len = math::maximum(match_hist_max_len, hist_match_len);

               approx_state.get_rep_match_costs(cur_dict_ofs, lzdec_bitcosts, rep_match_index, match_hist_min_match_len, hist_match_len, is_match_model_index);

               uint rep_match_total_complexity = cur_node_total_complexity + (cRep0Complexity + rep_match_index);
               for (uint l = match_hist_min_match_len; l <= hist_match_len; l++)
               {
#if LZHAM_VERIFY_MATCH_COSTS
                  {
                     lzdecision actual_dec(cur_dict_ofs, l, -((int)rep_match_index + 1));
                     bit_cost_t actual_cost = approx_state.get_cost(*this, m_accel, actual_dec);
                     LZHAM_ASSERT(actual_cost == lzdec_bitcosts[l]);
                  }
#endif
                  node_state& dst_node = pCur_node[l];

                  bit_cost_t rep_match_total_cost = cur_node_total_cost + lzdec_bitcosts[l];

                  if ((rep_match_total_cost > dst_node.m_total_cost) || ((rep_match_total_cost == dst_node.m_total_cost) && (rep_match_total_complexity >= dst_node.m_total_complexity)))
                     continue;

                  dst_node.m_total_cost = rep_match_total_cost;
                  dst_node.m_total_complexity = rep_match_total_complexity;
                  dst_node.m_parent_index = (uint16)cur_node_index;
                  approx_state.save_partial_state(dst_node.m_saved_state);
                  dst_node.m_lzdec.init(cur_dict_ofs, l, -((int)rep_match_index + 1));
                  dst_node.m_lzdec.m_len = l;
               }
            }

            match_hist_min_match_len = cMinMatchLen;
         }

         uint max_match_len = match_hist_max_len;

         if (max_match_len >= m_settings.m_fast_bytes)
         {
            cur_dict_ofs += max_match_len;
            cur_lookahead_ofs += max_match_len;
            cur_node_index += max_match_len;
            continue;
         }

         // full matches
         if (max_admissable_match_len >= cMinMatchLen)
         {
            uint num_full_matches = 0;

            if (match_hist_max_len < 2)
            {
               uint len2_match_dist = m_accel.get_len2_match(cur_lookahead_ofs);
               if (len2_match_dist)
               {
                  bit_cost_t cost = approx_state.get_len2_match_cost(*this, cur_dict_ofs, len2_match_dist, is_match_model_index);

#if LZHAM_VERIFY_MATCH_COSTS
                  {
                     lzdecision actual_dec(cur_dict_ofs, 2, len2_match_dist);
                     bit_cost_t actual_cost = approx_state.get_cost(*this, m_accel, actual_dec);
                     LZHAM_ASSERT(actual_cost == cost);
                  }
#endif

                  node_state& dst_node = pCur_node[2];

                  bit_cost_t match_total_cost = cur_node_total_cost + cost;
                  uint match_total_complexity = cur_node_total_complexity + cShortMatchComplexity;

                  if ((match_total_cost < dst_node.m_total_cost) || ((match_total_cost == dst_node.m_total_cost) && (match_total_complexity < dst_node.m_total_complexity)))
                  {
                     dst_node.m_total_cost = match_total_cost;
                     dst_node.m_total_complexity = match_total_complexity;
                     dst_node.m_parent_index = (uint16)cur_node_index;
                     approx_state.save_partial_state(dst_node.m_saved_state);
                     dst_node.m_lzdec.init(cur_dict_ofs, 2, len2_match_dist);
                  }

                  max_match_len = 2;
               }
            }

            const uint min_truncate_match_len = max_match_len;

            const dict_match* pMatches = m_accel.find_matches(cur_lookahead_ofs);
            if (pMatches)
            {
               for ( ; ; )
               {
                  uint match_len = pMatches->get_len();
                  LZHAM_ASSERT((pMatches->get_dist() > 0) && (pMatches->get_dist() <= m_dict_size));
                  match_len = LZHAM_MIN(match_len, max_admissable_match_len);

                  if (match_len > max_match_len)
                  {
                     max_match_len = match_len;

                     match_lens[num_full_matches] = match_len;
                     match_distances[num_full_matches] = pMatches->get_dist();
                     num_full_matches++;
                  }

                  if (pMatches->is_last())
                     break;
                  pMatches++;
               }
            }

            if (num_full_matches)
            {
               uint prev_max_match_len = LZHAM_MAX(1, min_truncate_match_len); //match_hist_max_len);
               for (uint full_match_index = 0; full_match_index < num_full_matches; full_match_index++)
               {
                  uint start_len = prev_max_match_len + 1;
                  uint end_len = match_lens[full_match_index];
                  uint match_dist = match_distances[full_match_index];

                  LZHAM_ASSERT(start_len <= end_len);

                  approx_state.get_full_match_costs(*this, cur_dict_ofs, lzdec_bitcosts, match_dist, start_len, end_len, is_match_model_index);

                  for (uint l = start_len; l <= end_len; l++)
                  {
                     uint match_complexity = (l >= cLongMatchComplexityLenThresh) ? cLongMatchComplexity : cShortMatchComplexity;

#if LZHAM_VERIFY_MATCH_COSTS
                     {
                        lzdecision actual_dec(cur_dict_ofs, l, match_dist);
                        bit_cost_t actual_cost = approx_state.get_cost(*this, m_accel, actual_dec);
                        LZHAM_ASSERT(actual_cost == lzdec_bitcosts[l]);
                     }
#endif
                     node_state& dst_node = pCur_node[l];

                     bit_cost_t match_total_cost = cur_node_total_cost + lzdec_bitcosts[l];
                     uint match_total_complexity = cur_node_total_complexity + match_complexity;

                     if ((match_total_cost > dst_node.m_total_cost) || ((match_total_cost == dst_node.m_total_cost) && (match_total_complexity >= dst_node.m_total_complexity)))
                        continue;

                     dst_node.m_total_cost = match_total_cost;
                     dst_node.m_total_complexity = match_total_complexity;
                     dst_node.m_parent_index = (uint16)cur_node_index;
                     approx_state.save_partial_state(dst_node.m_saved_state);
                     dst_node.m_lzdec.init(cur_dict_ofs, l, match_dist);
                  }

                  prev_max_match_len = end_len;
               }
            }
         }

         if (max_match_len >= m_settings.m_fast_bytes)
         {
            cur_dict_ofs += max_match_len;
            cur_lookahead_ofs += max_match_len;
            cur_node_index += max_match_len;
            continue;
         }

         // literal
         bit_cost_t lit_cost = approx_state.get_lit_cost(m_accel, cur_dict_ofs, lit_pred0, is_match_model_index);
         bit_cost_t lit_total_cost = cur_node_total_cost + lit_cost;
         uint lit_total_complexity = cur_node_total_complexity + cLitComplexity;
#if LZHAM_VERIFY_MATCH_COSTS
         {
            lzdecision actual_dec(cur_dict_ofs, 0, 0);
            bit_cost_t actual_cost = approx_state.get_cost(*this, m_accel, actual_dec);
            LZHAM_ASSERT(actual_cost == lit_cost);
         }
#endif
         if ((lit_total_cost < pCur_node[1].m_total_cost) || ((lit_total_cost == pCur_node[1].m_total_cost) && (lit_total_complexity < pCur_node[1].m_total_complexity)))
         {
            pCur_node[1].m_total_cost = lit_total_cost;
            pCur_node[1].m_total_complexity = lit_total_complexity;
            pCur_node[1].m_parent_index = (int16)cur_node_index;
            approx_state.save_partial_state(pCur_node[1].m_saved_state);
            pCur_node[1].m_lzdec.init(cur_dict_ofs, 0, 0);
         }

         cur_dict_ofs++;
         cur_lookahead_ofs++;
         cur_node_index++;

      } // graph search

      // Now get the optimal decisions by starting from the goal node.
      // m_best_decisions is filled backwards.
      if (!parse_state.m_best_decisions.try_reserve(bytes_to_parse))
      {
         parse_state.m_failed = true;
         return false;
      }

      int node_index = bytes_to_parse;
      lzdecision *pDst_dec = parse_state.m_best_decisions.get_ptr();
      do
      {
         LZHAM_ASSERT((node_index >= 0) && (node_index <= (int)cMaxParseGraphNodes));
         node_state& cur_node = pNodes[node_index];

         *pDst_dec++ = cur_node.m_lzdec;

         node_index = cur_node.m_parent_index;

      } while (node_index > 0);

      parse_state.m_best_decisions.try_resize(static_cast<uint>(pDst_dec - parse_state.m_best_decisions.get_ptr()));

      return true;
   }

   void lzcompressor::parse_job_callback(uint64 data, void* pData_ptr)
   {
      const uint parse_job_index = (uint)data;
      scoped_perf_section parse_job_timer(cVarArgs, "parse_job_callback %u", parse_job_index);

      (void)pData_ptr;

      parse_thread_state &parse_state = m_parse_thread_state[parse_job_index];

#if 0
      if (m_params.m_compression_level == cCompressionLevelFastest)
         greedy_parse(parse_state);
      else
#endif
      if ((m_params.m_lzham_compress_flags & LZHAM_COMP_FLAG_EXTREME_PARSING) && (m_params.m_compression_level == cCompressionLevelUber))
         extreme_parse(parse_state);
      else
         optimal_parse(parse_state);

      LZHAM_MEMORY_EXPORT_BARRIER

      if (atomic_decrement32(&m_parse_jobs_remaining) == 0)
      {
         m_parse_jobs_complete.release();
      }
   }

   bool lzcompressor::compress_block(const void* pBuf, uint buf_len)
   {
      scoped_perf_section compress_block_timer(cVarArgs, "****** compress_block %u", m_block_index);

      LZHAM_ASSERT(pBuf);
      LZHAM_ASSERT(buf_len <= m_params.m_block_size);

      LZHAM_ASSERT(m_src_size >= 0);
      if (m_src_size < 0)
         return false;

      m_src_size += buf_len;

      if (!m_accel.add_bytes_begin(buf_len, static_cast<const uint8*>(pBuf)))
         return false;

      m_src_adler32 = adler32(pBuf, buf_len, m_src_adler32);

      m_block_start_dict_ofs = m_accel.get_lookahead_pos() & (m_accel.get_max_dict_size() - 1);

      uint cur_dict_ofs = m_block_start_dict_ofs;

      uint bytes_to_match = buf_len;

      if (!m_codec.start_encoding((buf_len * 9) / 8))
         return false;

      if (!m_block_index)
      {
         if (!send_configuration())
            return false;
      }

#ifdef LZHAM_LZDEBUG
      m_codec.encode_bits(166, 12);
#endif

      if (!m_codec.encode_bits(cCompBlock, cBlockHeaderBits))
         return false;

      if (!m_codec.encode_arith_init())
         return false;

      m_state.start_of_block(m_accel, cur_dict_ofs, m_block_index);

      m_initial_state = m_state;

      coding_stats initial_stats(m_stats);

      uint initial_step = m_step;

#if 0
#ifdef LZHAM_LZVERIFY
      // TODO: This no longer works.
      lzham::vector<lzdecision> lzdecisions0;
      if (!lzdecisions0.try_reserve(64))
         return false;

      for (uint i = 0; i < bytes_to_match; i++)
      {
         uint cur_dict_ofs = m_block_start_dict_ofs + i;
         int largest_match_index = enumerate_lz_decisions(cur_dict_ofs, m_state, lzdecisions0, 1);
         if (largest_match_index < 0)
            return false;

         bit_cost_t largest_match_cost = lzdecisions0[largest_match_index].m_cost;
         uint largest_match_len = lzdecisions0[largest_match_index].get_len();

         for (uint j = 0; j < lzdecisions0.size(); j++)
         {
            const lzdecision& lzdec = lzdecisions0[j];

            if (lzdec.is_match())
            {
               uint match_dist = lzdec.get_match_dist(m_state);

               for (uint k = 0; k < lzdec.get_len(); k++)
               {
                  LZHAM_VERIFY(m_accel[cur_dict_ofs+k] == m_accel[(cur_dict_ofs+k - match_dist) & (m_accel.get_max_dict_size() - 1)]);
               }
            }
         }
      }
#endif
#endif

      while (bytes_to_match)
      {
         uint num_parse_jobs = LZHAM_MIN(m_num_parse_threads, (bytes_to_match + cMaxParseGraphNodes - 1) / cMaxParseGraphNodes);
         if ((m_params.m_lzham_compress_flags & LZHAM_COMP_FLAG_DETERMINISTIC_PARSING) == 0)
         {
            if (m_use_task_pool && m_accel.get_max_helper_threads())
            {
               // Increase the number of parser threads as the match finder finishes up.
               num_parse_jobs += m_accel.get_num_completed_helper_threads();
               num_parse_jobs = LZHAM_MIN(num_parse_jobs, cMaxParseThreads);
            }
         }
         if (bytes_to_match < 1536)
            num_parse_jobs = 1;

         // Reduce block size near the beginning of the file so statistical models get going a bit faster.
         bool force_small_block = false;
         if ( (!m_block_index) && ((cur_dict_ofs - m_block_start_dict_ofs) < cMaxParseGraphNodes) )
         {
            num_parse_jobs = 1;
            force_small_block = true;
         }

         uint parse_thread_start_ofs = cur_dict_ofs;
         uint parse_thread_total_size = LZHAM_MIN(bytes_to_match, cMaxParseGraphNodes * num_parse_jobs);
         if (force_small_block)
         {
            parse_thread_total_size = LZHAM_MIN(parse_thread_total_size, 1536);
         }

         uint parse_thread_remaining = parse_thread_total_size;
         for (uint parse_thread_index = 0; parse_thread_index < num_parse_jobs; parse_thread_index++)
         {
            parse_thread_state &parse_thread = m_parse_thread_state[parse_thread_index];

            parse_thread.m_approx_state = m_state;
            parse_thread.m_approx_state.m_cur_ofs = parse_thread_start_ofs;

            if (parse_thread_index > 0)
            {
               parse_thread.m_approx_state.reset_state_partial();
               parse_thread.m_issued_reset_state_partial = true;
            }
            else
            {
               parse_thread.m_issued_reset_state_partial = false;
            }

            parse_thread.m_start_ofs = parse_thread_start_ofs;
            if (parse_thread_index == (num_parse_jobs - 1))
               parse_thread.m_bytes_to_match = parse_thread_remaining;
            else
               parse_thread.m_bytes_to_match = parse_thread_total_size / num_parse_jobs;

            parse_thread.m_bytes_to_match = LZHAM_MIN(parse_thread.m_bytes_to_match, cMaxParseGraphNodes);
            LZHAM_ASSERT(parse_thread.m_bytes_to_match > 0);

            parse_thread_start_ofs += parse_thread.m_bytes_to_match;
            parse_thread_remaining -= parse_thread.m_bytes_to_match;
         }

         {
            scoped_perf_section parse_timer("parsing");

            if ((m_use_task_pool) && (num_parse_jobs > 1))
            {
               m_parse_jobs_remaining = num_parse_jobs;

               {
                  scoped_perf_section queue_task_timer("queing parse tasks");

                  if (!m_params.m_pTask_pool->queue_multiple_object_tasks(this, &lzcompressor::parse_job_callback, 1, num_parse_jobs - 1))
                     return false;
               }

               parse_job_callback(0, NULL);

               {
                  scoped_perf_section wait_timer("waiting for jobs");

                  m_parse_jobs_complete.wait();
               }
            }
            else
            {
               m_parse_jobs_remaining = INT_MAX;
               for (uint parse_thread_index = 0; parse_thread_index < num_parse_jobs; parse_thread_index++)
               {
                  parse_job_callback(parse_thread_index, NULL);
               }
            }
         }

         {
            scoped_perf_section coding_timer("coding");

            for (uint parse_thread_index = 0; parse_thread_index < num_parse_jobs; parse_thread_index++)
            {
               parse_thread_state &parse_thread = m_parse_thread_state[parse_thread_index];
               if (parse_thread.m_failed)
                  return false;

               const lzham::vector<lzdecision> &best_decisions = parse_thread.m_best_decisions;

               if (parse_thread.m_issued_reset_state_partial)
               {
                  if (!m_state.encode_reset_state_partial(m_codec, m_accel))
                     return false;
                  m_step++;
               }

               if (best_decisions.size())
               {
                  int i = 0;
                  int end_dec_index = static_cast<int>(best_decisions.size()) - 1;
                  int dec_step = 1;
                  if (parse_thread.m_emit_decisions_backwards)
                  {

                     i = static_cast<int>(best_decisions.size()) - 1;
                     end_dec_index = 0;

                     dec_step = -1;
                  }

                  LZHAM_ASSERT(best_decisions.back().m_pos == (int)parse_thread.m_start_ofs);

                  // Loop rearranged to avoid bad x64 codegen problem with MSVC2008.
                  for ( ; ; )
                  {
                     LZHAM_ASSERT(best_decisions[i].m_pos == (int)cur_dict_ofs);
                     LZHAM_ASSERT(i >= 0);
                     LZHAM_ASSERT(i < (int)best_decisions.size());

#if LZHAM_UPDATE_STATS
                     bit_cost_t cost = m_state.get_cost(*this, m_accel, best_decisions[i]);
                     m_stats.update(best_decisions[i], m_state, m_accel, cost);
#endif

                     if (!code_decision(best_decisions[i], cur_dict_ofs, bytes_to_match))
                        return false;
                     if (i == end_dec_index)
                        break;
                     i += dec_step;
                  }
               }

               LZHAM_ASSERT(cur_dict_ofs == parse_thread.m_start_ofs + parse_thread.m_bytes_to_match);

            } // parse_thread_index

         }
      }

      {
         scoped_perf_section add_bytes_timer("add_bytes_end");
         m_accel.add_bytes_end();
      }

      if (!m_state.encode_eob(m_codec, m_accel))
         return false;

#ifdef LZHAM_LZDEBUG
      if (!m_codec.encode_bits(366, 12)) return false;
#endif

      {
         scoped_perf_section stop_encoding_timer("stop_encoding");
         if (!m_codec.stop_encoding(true)) return false;
      }

      uint compressed_size = m_codec.get_encoding_buf().size();
      compressed_size;

#if defined(LZHAM_DISABLE_RAW_BLOCKS) || defined(LZHAM_LZDEBUG)
      if (0)
#else
      if (compressed_size >= buf_len)
#endif
      {
         m_state = m_initial_state;
         m_step = initial_step;
         //m_stats = initial_stats;

         m_codec.clear();

         if (!m_codec.start_encoding(buf_len + 16)) return false;

         if (!m_block_index)
         {
            if (!send_configuration())
               return false;
         }

#ifdef LZHAM_LZDEBUG
         if (!m_codec.encode_bits(166, 12)) return false;
#endif

         if (!m_codec.encode_bits(cRawBlock, cBlockHeaderBits)) return false;

         LZHAM_ASSERT(buf_len <= 0x1000000);
         if (!m_codec.encode_bits(buf_len - 1, 24)) return false;
         if (!m_codec.encode_align_to_byte()) return false;

         const uint8* pSrc = m_accel.get_ptr(m_block_start_dict_ofs);

         for (uint i = 0; i < buf_len; i++)
         {
            if (!m_codec.encode_bits(*pSrc++, 8)) return false;
         }

         if (!m_codec.stop_encoding(true)) return false;
      }

      {
         scoped_perf_section append_timer("append");

         if (m_comp_buf.empty())
         {
            m_comp_buf.swap(m_codec.get_encoding_buf());
         }
         else
         {
            if (!m_comp_buf.append(m_codec.get_encoding_buf()))
               return false;
         }
      }
#if LZHAM_UPDATE_STATS
      LZHAM_VERIFY(m_stats.m_total_bytes == m_src_size);
#endif

      m_block_index++;

      return true;
   }

} // namespace lzham
