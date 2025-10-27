cmake_minimum_required(VERSION 3.18)
project(INDI_tutorial_four CXX C)


########### Tutorial four ##############
add_executable(tutorial_four simpleskeleton.cpp)

target_link_libraries(tutorial_four indidriver)

