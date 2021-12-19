cmake_minimum_required(VERSION 3.12)
project(async)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-Wall -Werror -std=c++17")


include(FetchContent)

FetchContent_Declare(googletest
  GIT_REPOSITORY    https://github.com/google/googletest.git
  GIT_TAG           release-1.11.0
  SOURCE_DIR        "${CMAKE_CURRENT_BINARY_DIR}/googletest-src"
  BINARY_DIR        "${CMAKE_CURRENT_BINARY_DIR}/googletest-build"
)

# Prevent overriding the parent project's compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)


include_directories(${PROJECT_SOURCE_DIR}/src)

add_library(async
            STATIC
            src/v1/async.cpp
            src/worker.cpp
            src/workerpool.cpp
            src/v3/refcounter.cpp
            src/v3/refcounter.hpp
)

add_executable(async_test
               tests/async_test.cpp
               tests/callbackv1_test.cpp
               tests/worker_test.cpp
               tests/callbackv2_test.cpp
               tests/callbackv3_test.cpp
)
target_link_libraries(async_test
                      async
                      gtest_main)

enable_testing()
add_test(NAME async_test COMMAND async_test)
