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
        public_include
    FLAGS
        -Wno-format-nonliteral
    USES
        czmq
        fty_proto
        cxxtools
        fty_common
        fty-common-messagebus2-mqtt
        fty_common_logging
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

########################################################################################################################

# -- see fty-shm-cleanup/resources .conf file --
#create run/42shm/0 (see FTY_SHM_METRIC_TYPE)
#owner & privileges: /run/42shm/0 0777 bios root
#install(DIRECTORY DESTINATION /run/42shm/0)

#if (BUILD_TESTING)
#
#    etn_test_target(${PROJECT_NAME}
#        SOURCES
#            tests/main.cc
#            src/*.cc
#            src/*.h
#        PREPROCESSOR -DCATCH_CONFIG_FAST_COMPILE
#        USES
#            czmq
#            fty_proto
##            mosquitto #pub mqtt mosquitto lib client
##            fty_common_messagebus_evol #pub mqtt fty-common-messagebus client
##            fty_common_messagebus2_mqtt fty_common_messagebus2 #pub mqtt fty-common-messagebus2 client
#            cxxtools fty_common fty_common_logging #mqtt publisher
#            Catch2::Catch2
#            stdc++fs
#            log4cplus
#    )
#
#    ## manual set of include dirs, can't be set in the etn_target_test macro
#    get_target_property(INCLUDE_DIRS_TARGET ${PROJECT_NAME} INCLUDE_DIRECTORIES)
#    target_include_directories(${PROJECT_NAME}-test PRIVATE ${INCLUDE_DIRS_TARGET})
#    target_include_directories(${PROJECT_NAME}-coverage PRIVATE ${INCLUDE_DIRS_TARGET})
#
#    #build selftest-rw/0 for test outputs (see FTY_SHM_METRIC_TYPE)
#    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/selftest-rw/0)
#
#    #build selftest-rw/0 for test outputs (see FTY_SHM_METRIC_TYPE)
#    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/selftest-rw/0)
#
#endif()

