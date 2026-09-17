# Alvo duplo: -DCORUJA_TESTES=ON compila a suite no host (sem Pico SDK);
# sem a opcao, compila o firmware para o RP2350. Ver docs/adr/0001.
cmake_minimum_required(VERSION 3.13)

option(CORUJA_TESTES "Compila a suite de testes no host em vez do firmware" OFF)
option(CORUJA_COBERTURA "Instrumenta para cobertura (requer CORUJA_TESTES)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CORUJA_TESTES)
    project(coruja_gps_testes CXX)
    if(CORUJA_COBERTURA)
        add_compile_options(--coverage -O0 -g)
        add_link_options(--coverage)
    endif()
    enable_testing()
    add_subdirectory(src)
    add_subdirectory(test)
else()
    include(pico_sdk_import.cmake)
    project(coruja_gps C CXX ASM)
    pico_sdk_init()
    add_subdirectory(src)
    add_executable(coruja_gps src/main.cpp)
    target_link_libraries(coruja_gps PRIVATE
        coruja_nucleo coruja_log coruja_ihm coruja_portes pico_stdlib)
    pico_enable_stdio_usb(coruja_gps 1)
    pico_enable_stdio_uart(coruja_gps 0)
    pico_add_extra_outputs(coruja_gps)
endif()
