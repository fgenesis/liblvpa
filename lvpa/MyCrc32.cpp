#include "LVPACommon.h"
#include "MyCrc32.h"

LVPA_NAMESPACE_START

static uint32 _tab[256];
static bool _notab = true;

CRC32::CRC32()
: _crc(0xFFFFFFFF)
{
    if(_notab)
    {
        GenTab();
        _notab = false;
    }
}

void CRC32::GenTab(void)
{
    uint32 crc;
    for (uint16 i = 0; i < 256; i++)
    {
        crc = i;
        for (uint8 j = 8; j > 0; j--)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320L;
            else
                crc >>= 1;
        }
        _tab[i] = crc;
    }
}

void CRC32::Finalize(void)
{
    _crc ^= 0xFFFFFFFF;
}

void CRC32::Update(const void *buf, uint32 size)
{
    const uint8* b = (const uint8*)buf;
    for (uint32 i = 0; i < size; i++)
        _crc = ((_crc >> 8) & 0x00FFFFFF) ^ _tab[(_crc ^ *b++) & 0xFF];
}

LVPA_NAMESPACE_END
