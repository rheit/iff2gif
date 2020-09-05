cmake_minimum_required(VERSION 3.18)

project(iff2gif CXX)

add_executable(iff2gif
    iff2gif.cpp

    gifwrite.cpp
    iffread.cpp
    chunky.cpp
    planar.cpp
    ppunpack.cpp
    rotate.cpp
)
set_property(TARGET iff2gif PROPERTY CXX_STANDARD 17)
