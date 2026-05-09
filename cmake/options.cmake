# If the verbose mode is activated, CMake will log more
# information while building. This information may include
# statistics, dependencies and versions.
option(cmake_VERBOSE "Enable for verbose logging." ON)

# Enable LTO (Link Time Optimization) for better optimization
# Set to OFF to disable LTO
option(ENABLE_LTO "Enable Link Time Optimization (LTO)" ON)

# Enable profiling/timing logs for performance measurement
# Set to ON to enable performance logging, OFF to disable
option(ENABLE_PROFILING "Enable profiling/timing logs" OFF)