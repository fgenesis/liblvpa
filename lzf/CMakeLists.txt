
set(lzf_SRC
crc32.h
lzf.h
lzfP.h
lzf_c.c
lzf_d.c
)

add_library(lzf ${lzf_SRC})
