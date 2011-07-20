
set(zlib_SRC
adler32.c
compress.c
crc32.c
crc32.h
deflate.c
deflate.h
infback.c
inffast.c
inffast.h
inffixed.h
inflate.c
inflate.h
inftrees.c
inftrees.h
trees.c
trees.h
uncompr.c
zconf.h
zlib.h
zutil.c
zutil.h
)

if(MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
endif()

add_library(zlib ${zlib_SRC})
