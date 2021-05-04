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
## https://cmake.org/cmake/help/v3.0/module/GNUInstallDirs.html

set(SERVICE_USER bios)

set(CONF_IN    ${CMAKE_CURRENT_SOURCE_DIR}/resources/fty-shm.conf.in)
set(SERVICE_IN ${CMAKE_CURRENT_SOURCE_DIR}/resources/service.in)
set(TIMER_IN   ${CMAKE_CURRENT_SOURCE_DIR}/resources/timer.in)

set(CONF    ${CMAKE_CURRENT_BINARY_DIR}/resources/fty-shm.conf)
set(SERVICE ${CMAKE_CURRENT_BINARY_DIR}/resources/${TARGET_NAME}.service)
set(TIMER   ${CMAKE_CURRENT_BINARY_DIR}/resources/${TARGET_NAME}.timer)

configure_file(${CONF_IN} ${CONF} @ONLY)
configure_file(${SERVICE_IN} ${SERVICE} @ONLY)
configure_file(${TIMER_IN} ${TIMER} @ONLY)

# .conf file -> lib/tmpfiles.d/
install(FILES ${CONF} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/tmpfiles.d/)
# lib/systemd/system
install(FILES ${SERVICE} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)
install(FILES ${TIMER} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)
