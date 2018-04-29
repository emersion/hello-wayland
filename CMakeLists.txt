cmake_minimum_required(VERSION 2.8.12)
project(hwl2018)

# Extra modules and scripts for CMake
# https://github.com/KDE/extra-cmake-modules
find_package(ECM REQUIRED)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_FIND_MODULE_DIR})

find_package(Wayland REQUIRED)
find_package(WaylandScanner REQUIRED)

find_package(PkgConfig)
pkg_check_modules(wayland-protocols REQUIRED wayland-protocols>=1.13)
execute_process(COMMAND pkg-config wayland-protocols --variable=pkgdatadir
    OUTPUT_VARIABLE WAYLAND_PROTOCOLS_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)

ecm_add_wayland_client_protocol(WL_PROT_SRC
    PROTOCOL ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml
    BASENAME xdg-shell)

include_directories(${CMAKE_BINARY_DIR})

add_executable(${PROJECT_NAME} main.c os-create-anonymous-file.c cat.c ${WL_PROT_SRC})
target_link_libraries(${PROJECT_NAME} ${Wayland_Client_LIBRARY})
