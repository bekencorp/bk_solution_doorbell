# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{ARMINO_TOOLS_PATH}/build_tools/cmake/project.cmake)

set(EXTRA_COMPONENTS_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components
)

# This project builds the smart_intercom leaf (CONFIG_SMART_INTERCOM=y). Because
# smart_lock's own gate CONFIG_SMART_LOCK is "depends on !SMART_INTERCOM", it is
# forced off here and smart_lock compiles to an empty library on its own - so its
# symbols cannot collide with smart_intercom and no EXCLUDE_COMPONENTS is needed.
project(app)
