cmake_minimum_required(VERSION 3.18)
project(INDI_tutorial_three CXX C)


########### Tutorial three ##############
if (CFITSIO_FOUND)
add_executable(tutorial_three simpleccd.cpp)

target_link_libraries(tutorial_three indidriver)
endif (CFITSIO_FOUND)

