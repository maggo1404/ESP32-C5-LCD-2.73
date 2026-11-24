# Host unit tests for the ITS decoder component (plain gcc/clang, no ESP-IDF).
#   cmake -S firmware/components/its/test -B build && cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.16)
project(its_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(ITS_SANITIZE "Build with AddressSanitizer + UBSan" ON)

set(ITS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_executable(its_test
    test_main.c
    ${ITS_DIR}/src/uper.c
    ${ITS_DIR}/src/its_frame.c
    ${ITS_DIR}/src/its_cam.c
    ${ITS_DIR}/src/its_denm.c
    ${ITS_DIR}/src/its_mapspat.c
    ${ITS_DIR}/src/its_names.c
    ${ITS_DIR}/src/its_store.c)
target_include_directories(its_test PRIVATE ${ITS_DIR}/include ${ITS_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(its_test PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Werror -g -O1)
if(ITS_SANITIZE)
    target_compile_options(its_test PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all
                                            -fno-omit-frame-pointer)
    target_link_options(its_test PRIVATE -fsanitize=address,undefined)
endif()
target_link_libraries(its_test PRIVATE m)

enable_testing()
add_test(NAME its_host COMMAND its_test)
