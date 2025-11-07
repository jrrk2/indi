rm -rf *  # Clean up first
mkdir -p cmake_modules

# Create our custom Find modules
echo 'set(NOVA_INCLUDE_DIR "/home/jonathan/libnova-0.15.0/src")
set(NOVA_LIBRARIES "/home/jonathan/libnova-0.15.0/build-win/src/.libs/libnova.a")
set(NOVA_FOUND TRUE)
message(STATUS "Using custom FindNova.cmake: ${NOVA_LIBRARIES}")' > cmake_modules/FindNova.cmake

echo 'set(CFITSIO_INCLUDE_DIR "/home/jonathan/vcpkg/installed/x64-mingw-static/include/cfitsio")
set(CFITSIO_LIBRARIES "/home/jonathan/vcpkg/installed/x64-mingw-static/lib/libcfitsio.a")
set(CFITSIO_FOUND TRUE)
message(STATUS "Using custom FindCFITSIO.cmake: ${CFITSIO_LIBRARIES}")' > cmake_modules/FindCFITSIO.cmake

echo 'set(FFTW3_INCLUDE_DIR "/home/jonathan/vcpkg/installed/x64-mingw-static/include")
set(FFTW3_LIBRARIES "/home/jonathan/vcpkg/installed/x64-mingw-static/lib/libfftw3.a")
set(FFTW3_FOUND TRUE)
message(STATUS "Using custom FindFFTW3.cmake: ${FFTW3_LIBRARIES}")' > cmake_modules/FindFFTW3.cmake

# Copy our Libev finder
cat > cmake_modules/FindLibev.cmake << 'EOF'
# - Try to find Libev
# Once done this will define
#  LIBEV_FOUND - System has libev
#  LIBEV_INCLUDE_DIRS - The libev include directories
#  LIBEV_LIBRARIES - The libraries needed to use libev

if(WIN32)
    # On Windows, make libev optional
    message(STATUS "Windows build: Making libev dependency optional")
    set(LIBEV_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/libev_win_compat")
    set(LIBEV_LIBRARY "")
    set(LIBEV_LIBRARIES "")
    set(LIBEV_FOUND TRUE)

    # Create compatibility directory if it doesn't exist
    file(MAKE_DIRECTORY ${LIBEV_INCLUDE_DIR})

    # Create a minimal stub ev.h for Windows
    file(WRITE ${LIBEV_INCLUDE_DIR}/ev.h "
    /* Minimal libev stub for Windows compatibility */
    #ifndef EV_H
    #define EV_H

    #include <windows.h>

    typedef struct ev_io {
        int fd;
        void* data;
    } ev_io;

    typedef struct ev_timer {
        void* data;
    } ev_timer;

    typedef void (*ev_io_cb)(struct ev_loop *loop, struct ev_io *w, int revents);
    typedef void (*ev_timer_cb)(struct ev_loop *loop, struct ev_timer *w, int revents);

    struct ev_loop;

    /* Define stub functions */
    #define EV_READ 1
    #define EV_WRITE 2
    #define EV_TIMEOUT 4

    static inline struct ev_loop* ev_default_loop(unsigned int flags) { return NULL; }
    static inline void ev_io_init(ev_io* w, ev_io_cb cb, int fd, int events) {}
    static inline void ev_io_start(struct ev_loop* loop, ev_io* w) {}
    static inline void ev_io_stop(struct ev_loop* loop, ev_io* w) {}
    static inline void ev_timer_init(ev_timer* w, ev_timer_cb cb, double after, double repeat) {}
    static inline void ev_timer_start(struct ev_loop* loop, ev_timer* w) {}
    static inline void ev_timer_stop(struct ev_loop* loop, ev_timer* w) {}
    static inline void ev_run(struct ev_loop* loop, int flags) {}

    #endif /* EV_H */
    ")
else()
    find_path(LIBEV_INCLUDE_DIR NAMES ev.h)
    find_library(LIBEV_LIBRARY NAMES ev)
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(Libev DEFAULT_MSG LIBEV_LIBRARY LIBEV_INCLUDE_DIR)
    mark_as_advanced(LIBEV_INCLUDE_DIR LIBEV_LIBRARY)
    set(LIBEV_LIBRARIES ${LIBEV_LIBRARY})
    set(LIBEV_INCLUDE_DIRS ${LIBEV_INCLUDE_DIR})
endif()
EOF

# Run cmake with server enabled
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=/home/jonathan/indi-3rdparty/indi-celestron-origin/mingw64-toolchain.cmake \
    -DCMAKE_MODULE_PATH=$(pwd)/cmake_modules \
    -DCMAKE_INSTALL_PREFIX=$HOME/mingw-indi/prefix \
    -DBUILD_SERVER=ON \
    -DBUILD_DRIVERS=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DZLIB_INCLUDE_DIR=/home/jonathan/vcpkg/installed/x64-mingw-static/include \
    -DZLIB_LIBRARY:FILEPATH=/home/jonathan/vcpkg/installed/x64-mingw-static/lib/libz.a \
    -DUSB1_INCLUDE_DIR=/home/jonathan/vcpkg/installed/x64-mingw-static/include \
    -DUSB1_LIBRARY:FILEPATH=/home/jonathan/vcpkg/installed/x64-mingw-static/lib/libusb-1.0.a \
    -DCURL_INCLUDE_DIR=/home/jonathan/vcpkg/installed/x64-mingw-static/include \
    -DCURL_LIBRARY:FILEPATH=/home/jonathan/vcpkg/installed/x64-mingw-static/lib/libcurl.a \
    -DGSL_INCLUDE_DIR=/home/jonathan/vcpkg/installed/x64-mingw-static/include \
    -DGSL_LIBRARY:FILEPATH=/home/jonathan/vcpkg/installed/x64-mingw-static/lib/libgsl.a \
    -DGSL_CBLAS_LIBRARY:FILEPATH=/home/jonathan/vcpkg/installed/x64-mingw-static/lib/libgslcblas.a \
    -DJPEG_INCLUDE_DIR=/home/jonathan/vcpkg/installed/x64-mingw-static/include \
    -DJPEG_LIBRARY:FILEPATH=/home/jonathan/vcpkg/installed/x64-mingw-static/lib/libjpeg.a


