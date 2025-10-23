cmake_minimum_required(VERSION 3.18)
project(INDI_tutorial_six CXX C)


########### Tutorial Six ##############
add_executable(tutorial_client tutorial_client.cpp)

target_link_libraries(tutorial_client indiclient ${ZLIB_LIBRARY} ${NOVA_LIBRARIES})
IF (UNIX AND NOT APPLE)
    target_link_libraries(tutorial_client -lpthread)
ENDIF ()
