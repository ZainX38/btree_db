cmake_minimum_required(VERSION 3.20)
project(btree_db C)

set(CMAKE_C_STANDARD 11)          # C11: modern, widely supported
set(CMAKE_C_STANDARD_REQUIRED ON) # fail rather than silently use an older standard
set(CMAKE_C_EXTENSIONS OFF)       # strict -std=c11, not GNU's dialect

# Warnings are bugs you haven't found yet. -Werror makes them stop the build.
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# In Debug builds, instrument the program to catch memory errors at runtime.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_executable(db src/main.c)
add_executable(playground src/playground.c
        src/playground.c)