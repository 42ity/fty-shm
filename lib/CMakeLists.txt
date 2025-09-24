cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

########################################################################################################################

etn_target(shared ${PROJECT_NAME}
    SOURCES
        src/*.cc
        src/*.h
    PUBLIC_HEADERS
        fty_shm.h
    PUBLIC_INCLUDE_DIR
        include
    FLAGS
        -Wno-format-nonliteral
    USES
        czmq
        fty_proto
        fty_common
        fty_common_logging
        fty-messagebus2-mqtt
)

set_target_properties(${PROJECT_NAME} PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})

########################################################################################################################
# -- see fty-shm-cleanup/resources .conf file --
#create run/42shm/0 (see FTY_SHM_METRIC_TYPE)
#owner & privileges: /run/42shm/0 0777 bios root
#install(DIRECTORY DESTINATION /run/42shm/0)

if (BUILD_TESTING)
    etn_test_target(${PROJECT_NAME}
        SOURCES
            tests/main.cpp
            tests/shm.cpp
        PREPROCESSOR
            -DCATCH_CONFIG_FAST_COMPILE
        SUBDIR
            tests
    )

    #build selftest-rw/0 for test outputs (see FTY_SHM_METRIC_TYPE)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/selftest-rw/0)

    #build selftest-rw/0 for test outputs (see FTY_SHM_METRIC_TYPE)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/selftest-rw/0)
endif()
