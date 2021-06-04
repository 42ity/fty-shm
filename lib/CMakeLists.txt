cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

########################################################################################################################

etn_target(shared ${PROJECT_NAME}
    PUBLIC_HEADERS
        fty_shm.h
    SOURCES
        src/fty_shm.cc
    PUBLIC_INCLUDE_DIR
        public_include
    FLAGS
        -Wno-format-nonliteral
    USES
        czmq
        fty_proto
)

set_target_properties(${PROJECT_NAME} PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})

########################################################################################################################

etn_test_target(${PROJECT_NAME}
    SOURCES
        tests/main.cpp
        tests/shm.cpp
    PREPROCESSOR
        -DCATCH_CONFIG_FAST_COMPILE
    SUBDIR
        tests
)

########################################################################################################################

# -- see fty-shm-cleanup/resources .conf file --
#create run/42shm/0 (see FTY_SHM_METRIC_TYPE)
#owner & privileges: /run/42shm/0 0777 bios root
#install(DIRECTORY DESTINATION /run/42shm/0)
