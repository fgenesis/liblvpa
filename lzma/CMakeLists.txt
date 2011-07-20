
set(lzma_SRC
LzFind.c
LzFind.h
LzHash.h
LzTypes.h
LzmaDec.c
LzmaDec.h
LzmaEnc.c
LzmaEnc.h
)

if(MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
endif()

add_library(lzma ${lzma_SRC})
