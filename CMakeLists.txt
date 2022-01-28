cmake_minimum_required(VERSION 3.0.0)
project(mtee VERSION 0.1.0)

include(CTest)
enable_testing()

add_executable(mtee
    source/args.cpp source/cpuload.cpp
    source/handler.cpp source/help.cpp source/helpers.cpp
    source/main.cpp source/output.cpp source/perr.cpp)

install(TARGETS mtee DESTINATION bin)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
