
cmake_minimum_required(VERSION 3.22)

set(PROJ "wl-client-vulkan")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_COMPILER "icx")
set(CMAKE_C_FLAGS "-Wall -std=c17")
set(CMAKE_C_FLAGS_DEBUG   "-O0 -g")
set(CMAKE_C_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_COMPILER "icpx")
set(CMAKE_CXX_FLAGS "-Wall -std=c++2b -sycl-std=2020")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(PROTOCOL_DIR "/usr/share/wayland-protocols/unstable/")


project(${PROJ})
find_package(IntelDPCPP REQUIRED)

add_custom_command(
  OUTPUT xdg-shell-v6-private.c
  COMMAND wayland-scanner client-header ${PROTOCOL_DIR}/xdg-shell/xdg-shell-unstable-v6.xml xdg-shell-v6-client.h
  COMMAND wayland-scanner private-code  ${PROTOCOL_DIR}/xdg-shell/xdg-shell-unstable-v6.xml xdg-shell-v6-private.c)

add_custom_command(
  OUTPUT zwp-tablet-v2-private.c    
  COMMAND wayland-scanner client-header ${PROTOCOL_DIR}/tablet/tablet-unstable-v2.xml zwp-tablet-v2-client.h
  COMMAND wayland-scanner private-code  ${PROTOCOL_DIR}/tablet/tablet-unstable-v2.xml zwp-tablet-v2-private.c)

include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}
  /opt/intel/oneapi/compiler/2022.2.0/linux/include/sycl/)

add_executable(${PROJ}
  ${PROJ}.cc
  ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-v6-private.c
  ${CMAKE_CURRENT_BINARY_DIR}/zwp-tablet-v2-private.c)

target_link_libraries(${PROJ}
  PRIVATE
  wayland-egl
  EGL
  OpenGL
  vulkan
  wayland-client)

add_custom_target(run
  DEPENDS ${PROJ}
  COMMAND WAYLAND_DEBUG=1 ./${PROJ})
