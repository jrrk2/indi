cmake_minimum_required(VERSION 3.16)
project(indi_origin CXX)

# Set Qt5 path for macOS Homebrew
set(CMAKE_PREFIX_PATH "/opt/homebrew/Cellar/qt@5/5.15.17/lib/cmake/Qt5" ${CMAKE_PREFIX_PATH})

# Enable automoc for Qt signal/slot processing
set(CMAKE_AUTOMOC ON)

find_package(Qt5 COMPONENTS Core Network Gui REQUIRED)  # Removed WebSockets
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)  # ADD for base64 encoding
find_package(TIFF REQUIRED)

set(ORIGIN_SRCS
    indi_origin.cpp
    OriginBackendSimple.cpp
    TelescopeDataProcessor.cpp
    SimpleWebSocket.cpp  # ADD THIS
)

# MOC the headers that have Q_OBJECT
qt5_wrap_cpp(ORIGIN_MOC_SRCS
    OriginBackendSimple.hpp
    TelescopeDataProcessor.hpp
)

add_executable(indi_origin_telescope ${ORIGIN_SRCS} ${ORIGIN_MOC_SRCS})

target_link_libraries(indi_origin_telescope
    ${INDI_LIBRARIES}
    indidriver
    ${TIFF_LIBRARIES}
    Qt5::Core
    Qt5::Network
    Qt5::Gui
    Threads::Threads
    OpenSSL::SSL  # ADD THIS
    OpenSSL::Crypto  # ADD THIS
)

install(TARGETS indi_origin_telescope RUNTIME DESTINATION bin)

# Install driver XML
if(INDI_DATA_DIR)
    install(FILES indi_origin.xml DESTINATION ${INDI_DATA_DIR})
else()
    install(FILES indi_origin.xml DESTINATION share/indi)
endif()
