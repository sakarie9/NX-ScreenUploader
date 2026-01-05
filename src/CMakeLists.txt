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

add_executable(${HOMEBREW_APP}.elf
        ${SOURCE_DIR}/main.cpp
        ${SOURCE_DIR}/album.cpp
        ${SOURCE_DIR}/upload.cpp
        ${SOURCE_DIR}/utils.cpp
        ${SOURCE_DIR}/config.cpp)

# Add conditional compile definitions for time functions
if (ENABLE_TIME_FUNCTIONS)
    target_compile_definitions(${HOMEBREW_APP}.elf PRIVATE ENABLE_TIME_FUNCTIONS)
    cmake_info("Time functions enabled")
else ()
    cmake_info("Time functions disabled")
endif ()
