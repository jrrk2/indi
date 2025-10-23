cmake_minimum_required(VERSION 3.18)
project(INDI_tutorial_one CXX C)


########### Tutorial one ##############
add_executable(tutorial_one simpledevice.cpp)

target_link_libraries(tutorial_one indidriver)


