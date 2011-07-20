#ifndef MYCRC32_H
#define MYCRC32_H

LVPA_NAMESPACE_START

class CRC32
{
public:
    CRC32();
    void Update(const void *buf, uint32 size);
    void Finalize(void);
    uint32 Result(void) { return _crc; }

    inline static uint32 Calc(const void *buf, uint32 size)
    {
        CRC32 crc;
        crc.Update(buf, size);
        crc.Finalize();
        return crc.Result();
    }

private:
    static void GenTab(void);
    uint32 _crc;
};

LVPA_NAMESPACE_END

#endif
