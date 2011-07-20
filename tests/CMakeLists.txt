
include_directories(${LVPA_SRC_DIR})

set(tests_SRC
LVPACipherTests.cpp
LVPACipherTests.h
LVPATests.cpp
LVPATests.h
main.cpp
)

if(LVPA_BUILD_TTVFS_BINDINGS)
    include_directories(${TTVFS_INCLUDE_DIRS})
    if(NOT TTVFS_SRC_DIR)
        message(ERROR "ttvfs source directory not specified")
    endif()
    file(GLOB ttvfs_SRC "${TTVFS_SRC_DIR}/*.cpp")
    list(APPEND tests_SRC ${ttvfs_SRC})
    source_group(ttvfs FILES ${ttvfs_SRC})
endif()

add_executable(tests ${tests_SRC})

if(LVPA_BUILD_TTVFS_BINDINGS)
    target_link_libraries(tests lvpa_ttvfs)
endif()

target_link_libraries(tests lvpa)
