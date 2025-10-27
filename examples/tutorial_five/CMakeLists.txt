cmake_minimum_required(VERSION 3.18)
project(INDI_tutorial_five CXX C)


########### Tutorial five - dome driver ##############
add_executable(tutorial_dome dome.cpp)

target_link_libraries(tutorial_dome indidriver)

########### Tutorial five - rain driver ##############
add_executable(tutorial_rain raindetector.cpp)

target_link_libraries(tutorial_rain indidriver)


