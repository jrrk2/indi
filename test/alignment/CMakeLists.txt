cmake_minimum_required(VERSION 3.18)
project(INDI_alignment CXX C)


INCLUDE_DIRECTORIES( ${INDI_INCLUDE_DIR} )

ADD_EXECUTABLE(test_alignment
    test_alignment.cpp
)

TARGET_LINK_LIBRARIES(test_alignment
    AlignmentDriver
    indidriver
    ${GTEST_BOTH_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

ADD_TEST(test-alignment test_alignment)
