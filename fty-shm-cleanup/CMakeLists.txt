cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

########################################################################################################################

set(TARGET_NAME fty-shm-cleanup)

#Create the target
etn_target(exe ${TARGET_NAME}
    SOURCES
        src/*.cc
        src/*.h
    USES
        fty_common_logging
)

# install

set(SERVICE_USER bios)
set(SERVICE_IN ${CMAKE_CURRENT_SOURCE_DIR}/resources/service.in)
set(TIMER_IN   ${CMAKE_CURRENT_SOURCE_DIR}/resources/timer.in)
set(SERVICE ${CMAKE_CURRENT_BINARY_DIR}/resources/${TARGET_NAME}.service)
set(TIMER   ${CMAKE_CURRENT_BINARY_DIR}/resources/${TARGET_NAME}.timer)

configure_file(${SERVICE_IN} ${SERVICE} @ONLY)
configure_file(${TIMER_IN} ${TIMER} @ONLY)

# lib/systemd/system
install(FILES ${SERVICE} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)
install(FILES ${TIMER} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)
