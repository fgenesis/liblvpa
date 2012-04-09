
# extend ttvfs includes, but only if something was set already
if(TTVFS_INCLUDE_DIRS)
    if(NOT "${TTVFS_INCLUDE_DIRS}" MATCHES ".*${CMAKE_CURRENT_SOURCE_DIR}.*")
        set(TTVFS_INCLUDE_DIRS ${TTVFS_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "ttvfs include directories [+LVPA]" FORCE)
    endif()
else()
    set(TTVFS_INCLUDE_DIRS "" CACHE PATH "ttvfs include directories" FORCE)
endif()

# required for the test program
if(LVPA_BUILD_TESTPROG AND NOT TTVFS_SRC_DIR)
    set(TTVFS_SRC_DIR "" CACHE PATH "ttvfs source directory" FORCE)
endif()

include_directories(${TTVFS_INCLUDE_DIRS})
include_directories(${LVPA_SRC_DIR})

set(lvpa_ttvfs_SRC
VFSDirLVPA.cpp
VFSDirLVPA.h
VFSFileLVPA.cpp
VFSFileLVPA.h
VFSLoaderLVPA.cpp
VFSLoaderLVPA.h
VFSLVPAArchiveLoader.cpp
VFSLVPAArchiveLoader.h
)

add_library(lvpa_ttvfs ${lvpa_ttvfs_SRC})

target_link_libraries(lvpa_ttvfs lvpa)
