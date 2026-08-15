cmake_minimum_required(VERSION 3.15)
project(NC2000 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 11)
#set(CMAKE_CXX_FLAGS "-O2 -g3 -fsanitize=address -I/opt/homebrew/include")
set(CMAKE_CXX_FLAGS "-O3 -g3 -pthread -DHANDYPSP -Wno-deprecated-declarations")
#set(CMAKE_CXX_FLAGS "-O3 -g3 -pthread -DHANDYPSP -Wall -Wextra -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-parameter -Wno-unused-private-field")
#set(CMAKE_CXX_FLAGS "-O2 -g3 -pthread -DHANDYPSP -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-deprecated-declarations")

find_package(SDL2 REQUIRED)
message(STATUS "sdl includes: ${SDL2_INCLUDE_DIRS}")
message(STATUS "sdl libs:   ${SDL2_LIBRARIES}")
include_directories(${SDL2_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/devices)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/desktop)

if(ENABLE_SERIAL_PORT)
    message(STATUS "serial port support enabled!")
    add_definitions(-DENABLE_SERIAL_PORT)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSERIALPORT REQUIRED libserialport)
    message(STATUS "libserialport cflags: ${LIBSERIALPORT_CFLAGS}")
    message(STATUS "libserialport includess: ${LIBSERIALPORT_INCLUDE_DIRS}")
    message(STATUS "libserialport libs:   ${LIBSERIALPORT_LIBRARIES}")
    message(STATUS "libserialport libdir:   ${LIBSERIALPORT_LIBDIR}")
    include_directories(${LIBSERIALPORT_INCLUDE_DIRS})
    link_directories(${LIBSERIALPORT_LIBDIR})
endif()

if(ENABLE_UDP_SERVER)
    message(STATUS "udp server support enabled!")
    add_definitions(-DENABLE_UDP_SERVER)
endif()

#target_link_libraries(a.out PRIVATE "-Wl,-stack_size -Wl,3f00000")
#target_link_directories(a.out PUBLIC "/opt/homebrew/lib")

set(NC2000_CORE_SOURCES
    src/core/comm.cpp
    src/core/cpu.cpp
    src/core/cpu_loop.cpp
    src/core/cpu_loop_new.cpp
    src/core/mem.cpp
    src/core/nc2000.cpp
    src/core/ram.cpp
)

set(NC2000_DEVICE_SOURCES
    src/devices/NekoDriverIO.cpp
    src/devices/display.cpp
    src/devices/io.cpp
    src/devices/io_new.cpp
    src/devices/iv_uart.cpp
    src/devices/key.cpp
    src/devices/key_new.cpp
    src/devices/nand.cpp
    src/devices/nor.cpp
    src/devices/rom.cpp
    src/devices/sound.cpp
)

set(NC2000_DESKTOP_SOURCES
    src/desktop/cmd.cpp
    src/desktop/console.cpp
    src/desktop/disassembler.cpp
    src/desktop/disassembler_new.cpp
    src/desktop/main.cpp
    src/desktop/settings.cpp
    src/desktop/udp_server.cpp
)

set(NC2000_SUPPORT_SOURCES
    dsp/dsp.cpp
    ansi/w65c02cpu.cpp
    ansi/w65c02op.cpp
    compare/c6502.cpp
    compare/pc1000bus.cpp
    lcdstripe/json.cpp
    lcdstripe/lcdpainter.cpp
)

add_executable(a.out
    ${NC2000_CORE_SOURCES}
    ${NC2000_DEVICE_SOURCES}
    ${NC2000_DESKTOP_SOURCES}
    ${NC2000_SUPPORT_SOURCES}
)
target_link_libraries(a.out ${SDL2_LIBRARIES})

if(ENABLE_UDP_SERVER)
    if(WIN32)
        target_link_libraries(a.out wsock32 ws2_32)
    endif()
endif()

if(ENABLE_SERIAL_PORT)
    target_compile_options(a.out PRIVATE ${LIBSERIALPORT_CFLAGS})
    target_link_libraries(a.out ${LIBSERIALPORT_LIBRARIES})
endif()
