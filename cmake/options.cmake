# If the verbose mode is activated, CMake will log more
# information while building. This information may include
# statistics, dependencies and versions.
option(cmake_VERBOSE "Enable for verbose logging." ON)

# Enable LTO (Link Time Optimization) for better optimization
# Set to OFF to disable LTO
option(ENABLE_LTO "Enable Link Time Optimization (LTO)" ON)

# Enable time-related functions (get_time, time initialization)
# Set to ON to enable time functionality, OFF to disable
option(ENABLE_TIME_FUNCTIONS "Enable time-related functions" OFF)