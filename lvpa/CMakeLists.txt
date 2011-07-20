
set(lvpa_SRC
ByteBuffer.h
ByteConverter.h
DeflateCompressor.cpp
DeflateCompressor.h
ICompressor.h
LVPAFile.cpp
LVPATools.cpp
LVPATools.h
${LVPA_INCLUDE_DIRS}/LVPAFile.h 
${LVPA_INCLUDE_DIRS}/LVPACommon.h
${LVPA_INCLUDE_DIRS}/LVPACompileConfig.h
LVPAStreamCipher.cpp
LVPAStreamCipher.h
LZFCompressor.cpp
LZFCompressor.h
LZMACompressor.cpp
LZMACompressor.h
LZOCompressor.cpp
LZOCompressor.h
MersenneTwister.h
MyCrc32.cpp
MyCrc32.h
ProgressBar.cpp
ProgressBar.h
SHA256Hash.cpp
SHA256Hash.h
sha256.cpp
sha256.h
)

set(LVPA_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "LVPA source dir - used for lvpak and ttvfs bindings" FORCE)

add_library(lvpa ${lvpa_SRC})

target_link_libraries(lvpa ${LVPA_DEP_LIBS})
