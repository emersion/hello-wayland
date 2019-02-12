#
# MIT License
#
# Copyright (c) 2018 Joel Winarske
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

cmake_minimum_required(VERSION 3.11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "MinSizeRel" CACHE STRING "Choose the type of build, options are: Debug, Release, or MinSizeRel." FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set, defaulting to MinSizeRel.")
endif()

project(hello_wayland LANGUAGES C)

message(STATUS "Generator .............. ${CMAKE_GENERATOR}")
message(STATUS "Build Type ............. ${CMAKE_BUILD_TYPE}")

find_program(convert REQUIRED)

include(FindPkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules (WAYLAND_CLIENT REQUIRED wayland-client)

    pkg_check_modules (WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
    if(WAYLAND_PROTOCOLS_FOUND)
        pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
    endif()
   
    pkg_check_modules (WAYLAND_SCANNER_TOOL REQUIRED wayland-scanner)
    if(WAYLAND_SCANNER_TOOL_FOUND)
        pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
    endif()
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -Wno-unused-parameter")

set(XDG_SHELL_PROTOCOL ${CMAKE_SYSROOT}${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml)
set(XDG_SHELL_FILES xdg-shell-client-protocol.h xdg-shell-protocol.c)
set(SHM_FILES shm.c shm.h)

add_custom_command(OUTPUT xdg-shell-client-protocol.h
    COMMAND ${CMAKE_SYSROOT}${WAYLAND_SCANNER} client-header ${XDG_SHELL_PROTOCOL}
        ${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM
)

add_custom_command(OUTPUT xdg-shell-protocol.c
    COMMAND ${CMAKE_SYSROOT}${WAYLAND_SCANNER} private-code ${XDG_SHELL_PROTOCOL}
        ${CMAKE_BINARY_DIR}/xdg-shell-protocol.c
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM
)

add_custom_command(OUTPUT cat.h
    COMMAND convert cat.png -define h:format=bgra -depth 8 ${CMAKE_BINARY_DIR}/cat.h
    DEPENDS ${CMAKE_SOURCE_DIR}/cat.png
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM
)
include_directories(${CMAKE_BINARY_DIR})

set(CMAKE_C_FLAGS "${CMAKE_CFLAGS} ${WAYLAND_CLIENT_CFLAGS}")

add_executable(hello-wayland main.c cat.h ${XDG_SHELL_FILES} ${SHM_FILES})
target_link_libraries(hello-wayland -lrt ${WAYLAND_CLIENT_LDFLAGS})
install(TARGETS hello-wayland RUNTIME DESTINATION bin)
