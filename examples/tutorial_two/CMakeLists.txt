cmake_minimum_required(VERSION 3.18)
project(INDI_tutorial_two CXX C)


########## Tutorial two ##############
add_executable(tutorial_two simplescope.cpp)

target_link_libraries(tutorial_two indidriver)


