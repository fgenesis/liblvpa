
include_directories(${LVPA_SRC_DIR})

add_executable(lvpak 
main.cpp
) 

target_link_libraries(lvpak lvpa)
