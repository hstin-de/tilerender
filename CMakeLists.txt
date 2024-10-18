cmake_minimum_required(VERSION 3.15)
project(tilerender)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(maplibre-native)

add_subdirectory(src)
