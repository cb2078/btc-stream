cmake_minimum_required(VERSION 3.15)
project(btc-stream CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(simdjson REQUIRED)

add_executable(btc-stream src/main.cc)

target_compile_definitions(btc-stream PRIVATE CINATRA_ENABLE_SSL)

target_link_libraries(btc-stream PRIVATE
  OpenSSL::Crypto
  OpenSSL::SSL
  simdjson::simdjson)

target_include_directories(btc-stream PRIVATE
  external/cinatra/include)

target_precompile_headers(btc-stream PRIVATE
  <cinatra.hpp>
  <simdjson.h>)
