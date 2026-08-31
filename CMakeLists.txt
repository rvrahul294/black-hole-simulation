cmake_minimum_required(VERSION 3.20)

project(BlackHole)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(black_hole black_hole.cpp)

target_link_libraries(black_hole
    glfw3
    glew32
    opengl32
    gdi32
)