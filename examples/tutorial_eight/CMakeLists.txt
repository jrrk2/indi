cmake_minimum_required(VERSION 3.18)
project(INDI_tutorial_eight CXX C)


########### Tutorial Eight ##############
if (CFITSIO_FOUND)
add_executable(tutorial_eight simple_receiver.cpp)

target_link_libraries(tutorial_eight indidriver)
endif (CFITSIO_FOUND)


