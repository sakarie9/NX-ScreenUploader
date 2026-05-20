set(SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)

configure_file(${SOURCE_DIR}/project.h.in ${SOURCE_DIR}/project.h)
file(GLOB SOURCE_FILES RELATIVE ${SOURCE_DIR}
        "*.c"
        "*.cc"
        "*.cpp"
        "*.h"
        "*.hpp")

if (cmake_VERBOSE)
    list(LENGTH SOURCE_FILES SOURCE_FILES_LENGTH)

    cmake_info("Loaded ${SOURCE_FILES_LENGTH} source file(s)")
endif ()

# Auto-discover channel source files - add new .cpp files to channels/
# to automatically include them in the build.
file(GLOB CHANNEL_SOURCES CONFIGURE_DEPENDS ${SOURCE_DIR}/channels/*.cpp)

add_executable(${HOMEBREW_APP}.elf
        ${SOURCE_DIR}/main.cpp
        ${SOURCE_DIR}/album.cpp
        ${SOURCE_DIR}/queue.cpp
        ${SOURCE_DIR}/upload.cpp
        ${SOURCE_DIR}/utils.cpp
        ${SOURCE_DIR}/config.cpp
        ${CHANNEL_SOURCES})

# Add source directory to include path so channel modules can use
# #include "upload.hpp", #include "config.hpp" etc.
target_include_directories(${HOMEBREW_APP}.elf PRIVATE ${SOURCE_DIR})

# Add conditional compile definitions for profiling/timing
if (ENABLE_PROFILING)
    target_compile_definitions(${HOMEBREW_APP}.elf PRIVATE ENABLE_PROFILING)
    cmake_info("Profiling enabled")
else ()
    cmake_info("Profiling disabled")
endif ()
