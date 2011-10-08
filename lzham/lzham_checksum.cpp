// File: lzham_checksum.cpp
// Originally from the public domain stb.h header.
#include "lzham_core.h"

namespace lzham
{
   uint adler32(const void* pBuf, size_t buflen, uint adler32)
   {
      const uint8* buffer = static_cast<const uint8*>(pBuf);
      
      const unsigned long ADLER_MOD = 65521;
      unsigned long s1 = adler32 & 0xffff, s2 = adler32 >> 16;
      size_t blocklen;
      unsigned long i;

      blocklen = buflen % 5552;
      while (buflen) {
         for (i=0; i + 7 < blocklen; i += 8) {
            s1 += buffer[0], s2 += s1;
            s1 += buffer[1], s2 += s1;
            s1 += buffer[2], s2 += s1;
            s1 += buffer[3], s2 += s1;
            s1 += buffer[4], s2 += s1;
            s1 += buffer[5], s2 += s1;
            s1 += buffer[6], s2 += s1;
            s1 += buffer[7], s2 += s1;

            buffer += 8;
         }

         for (; i < blocklen; ++i)
            s1 += *buffer++, s2 += s1;

         s1 %= ADLER_MOD, s2 %= ADLER_MOD;
         buflen -= blocklen;
         blocklen = 5552;
      }
      return (s2 << 16) + s1;
   }
  
} // namespace lzham

