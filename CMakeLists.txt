cmake_minimum_required(VERSION 3.13)
project(fat_library C)

set(CMAKE_C_STANDARD 99)

add_executable(fat_library main.c fat.c fat.h reader.c reader.h fat_types.h fat_utils.c fat_utils.h)