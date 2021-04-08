cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

##############################################################################################################

project(fty_shm VERSION 1.0.0)

########################################################################################################################

find_package(fty-cmake PATHS ${CMAKE_BINARY_DIR})

########################################################################################################################

# lib & tests
add_subdirectory(lib)

# fty-shm-cleanup
add_subdirectory(fty-shm-cleanup)

# fty-shm-cli
add_subdirectory(fty-shm-cli)
