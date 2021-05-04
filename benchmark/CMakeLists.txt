cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

########################################################################################################################

set(TARGET_NAME benchmark)

#Create the target
etn_target(exe ${TARGET_NAME}
    SOURCES
        src/*.cc
        src/*.h
    USES
        fty_proto
        ${PROJECT_NAME}
    PRIVATE
)
