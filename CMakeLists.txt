cmake_minimum_required(VERSION 2.6)
project(lvpa)


option(LVPA_ENABLE_ZLIB "Add zlib/deflate support" TRUE)
option(LVPA_ENABLE_LZMA "Add LZMA support" TRUE)
option(LVPA_ENABLE_LZO "Add LZO support" FALSE)
option(LVPA_ENABLE_LZF "Add LZF support" TRUE)
option(LVPA_ENABLE_LZHAM "Add LZHAM support" TRUE)

option(LVPA_BUILD_TTVFS_BINDINGS "Build bindings for ttvfs?" FALSE)
option(LVPA_BUILD_LVPAK "Build lvpak commandline utility?" TRUE)
option(LVPA_BUILD_TESTPROG "Build test program?" TRUE)

option(LVPA_USE_INTERNAL_ZLIB "Use included zlib" TRUE)
option(LVPA_USE_INTERNAL_LZMA "Use included LZMA libary" TRUE)
option(LVPA_USE_INTERNAL_LZO "Use included LZO library" TRUE)
option(LVPA_USE_INTERNAL_LZF "Use included LZF library" TRUE)
option(LVPA_USE_INTERNAL_LZHAM "Use included LZHAM library" TRUE)


# compiler specific things
if(MSVC)
    # MSVC builds require installed runtime library by default
    option(LVPA_STATIC_LIB "Link as static library without runtime dependencies (Note: To get rid of this setting with MSVC, the cmake cache must be cleared)" FALSE)
    add_definitions("/GR-") # run-time type info (RTTI) not required
    
    if(LVPA_STATIC_LIB)
        # this is ugly - hackfix compiler flags
        foreach(flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
            if(${flag_var} MATCHES "/MD")
                string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
            endif(${flag_var} MATCHES "/MD")
            if(${flag_var} MATCHES "/MDd")
                string(REGEX REPLACE "/MDd" "/MTd" ${flag_var} "${${flag_var}}")
            endif(${flag_var} MATCHES "/MDd")
        endforeach()
        
        # hackfix linker flags - no idea why, but MSVC will produce linker errors otherwise
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /NODEFAULTLIB")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /NODEFAULTLIB:msvcrt.lib,msvcrtd.lib") # not sure if this is correct
        set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:msvcrt.lib,msvcrtd.lib")
    endif()
else()
    # non-msvc needs build type - if no build type was provided, set a default one
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
    endif()
endif()


include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(LVPA_DEP_LIBS "")

if(LVPA_ENABLE_ZLIB)
    add_definitions("-DLVPA_SUPPORT_ZLIB")
    if(LVPA_USE_INTERNAL_ZLIB)
        add_subdirectory(zlib)
    endif()
    list(APPEND LVPA_DEP_LIBS zlib)
endif()

if(LVPA_ENABLE_LZO)
    add_definitions("-DLVPA_SUPPORT_LZO")
    if(LVPA_USE_INTERNAL_LZO)
        add_subdirectory(lzo)
    endif()
    list(APPEND LVPA_DEP_LIBS lzo)
endif()

if(LVPA_ENABLE_LZMA)
    add_definitions("-DLVPA_SUPPORT_LZMA")
    if(LVPA_USE_INTERNAL_LZMA)
        add_subdirectory(lzma)
    endif()
    list(APPEND LVPA_DEP_LIBS lzma)
endif()

if(LVPA_ENABLE_LZF)
    add_definitions("-DLVPA_SUPPORT_LZF")
    if(LVPA_USE_INTERNAL_LZF)
        add_subdirectory(lzf)
    endif()
    list(APPEND LVPA_DEP_LIBS lzf)
endif()

if(LVPA_ENABLE_LZHAM)
    add_definitions("-DLVPA_SUPPORT_LZHAM")
    if(LVPA_USE_INTERNAL_LZHAM)
        add_subdirectory(lzham)
    endif()
    list(APPEND LVPA_DEP_LIBS lzham)
endif()


add_subdirectory(include)
add_subdirectory(lvpa)


if(LVPA_BUILD_LVPAK)
    add_subdirectory(lvpak)
endif()

if(LVPA_BUILD_TTVFS_BINDINGS)
    add_definitions("-DLVPA_SUPPORT_TTVFS")
    add_subdirectory(lvpa_ttvfs)
endif()

if(LVPA_BUILD_TESTPROG)
    add_subdirectory(tests)
endif()
