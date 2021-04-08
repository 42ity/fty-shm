cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

########################################################################################################################

set(TARGET_NAME fty-shm-cli)

#Create the target
etn_target(exe ${TARGET_NAME}
    SOURCES
        src/*.cc
        src/*.h
    USES
        czmq
        fty_proto
        fty_common_logging
        ${PROJECT_NAME}
)

# install logger file in etc/fty-shm-cli/
set(DEST_DIR ${CMAKE_INSTALL_FULL_SYSCONFDIR}/${TARGET_NAME})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/fty-shm-cli.logger DESTINATION ${DEST_DIR})
