cmake_minimum_required(VERSION 3.16)

project(iff2gif)

set(SOURCES
    iff2gif.cpp

    gifwrite.cpp
    iffread.cpp
    chunky.cpp
    planar.cpp
    ppunpack.cpp
    rotate.cpp
    mediancut.cpp
    neuquant.cpp
    quantizer.cpp
    palette.cpp
)

if(WIN32)
    list(APPEND SOURCES getopt.c)
endif()

add_executable(iff2gif ${SOURCES})

set_property(TARGET iff2gif PROPERTY CXX_STANDARD 17)
