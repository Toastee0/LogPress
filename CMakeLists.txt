cmake_minimum_required(VERSION 3.16)
project(logpilot C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Compiler warnings ---
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --- Platform defines ---
if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# ============================================================
# Vendored libraries
# ============================================================

# --- tiny-regex-c (regex matching) ---
add_library(tiny_regex STATIC
    vendor/tiny-regex-c/re.c
)
target_include_directories(tiny_regex PUBLIC vendor/tiny-regex-c)

# ============================================================
# LogPilot core library
# ============================================================
file(GLOB LOGPILOT_CORE_SOURCES src/lib/*.c)
add_library(logpilot_core STATIC ${LOGPILOT_CORE_SOURCES})
target_include_directories(logpilot_core
    PUBLIC src/lib
)
target_link_libraries(logpilot_core PUBLIC tiny_regex)

# ============================================================
# Executables
# ============================================================

add_executable(logparse  src/logparse.c)
target_link_libraries(logparse PRIVATE logpilot_core)

add_executable(logexplore src/logexplore.c)
target_link_libraries(logexplore PRIVATE logpilot_core)

add_executable(logfix src/logfix.c)
target_link_libraries(logfix PRIVATE logpilot_core)

# ============================================================
# Install
# ============================================================
install(TARGETS logparse logexplore logfix RUNTIME DESTINATION bin)
install(DIRECTORY modes/  DESTINATION share/logpilot/modes)
install(DIRECTORY fixes/  DESTINATION share/logpilot/fixes)
install(DIRECTORY schema/ DESTINATION share/logpilot/schema)

# ============================================================
# Tests
# ============================================================
enable_testing()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()
