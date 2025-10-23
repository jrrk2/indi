cmake_minimum_required(VERSION 3.18)
project(INDI_tutorial_seven CXX C)


########### Tutorial Seven ##############
add_executable(tutorial_seven simple_telescope_simulator.cpp)

target_link_libraries(tutorial_seven AlignmentDriver indidriver)


