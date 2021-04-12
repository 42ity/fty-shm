cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

########################################################################################################################

#Create the target
etn_target(shared ${PROJECT_NAME}
    SOURCES
        src/*.cc
        src/*.h
    PUBLIC_INCLUDE_DIR
        public_include
    PUBLIC_HEADERS
        fty_shm.h
    USES
        czmq
        fty_proto
)

#create run/42shm/0 (see FTY_SHM_METRIC_TYPE)
#FIXME owner & privileges: /run/42shm/0 0777 bios root
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_PREFIX}/run/42shm/0)

if (BUILD_TESTING)

    etn_test(${PROJECT_NAME}-test
        SOURCES
            tests/main.cc
            src/*.cc
            src/*.h
        INCLUDE_DIR
            include_public
        PREPROCESSOR -DCATCH_CONFIG_FAST_COMPILE
        USES
            czmq
            fty_proto
            Catch2::Catch2
            stdc++fs
            log4cplus
    )

    #build selftest-rw/0 for test outputs (see FTY_SHM_METRIC_TYPE)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/selftest-rw/0)

    #enable coverage
    etn_coverage(${PROJECT_NAME}-test)

    #build selftest-rw/0 for test outputs (see FTY_SHM_METRIC_TYPE)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/selftest-rw/0)

endif()
