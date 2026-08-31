cmake_minimum_required(VERSION 3.20)

project(BlackHole)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(black_hole_simulation_3d
    src/black_hole_simulation_3d.cpp
)

target_link_libraries(black_hole_simulation_3d
    glfw3
    glew32
    opengl32
    gdi32
)

add_executable(black_hole_lensing_2d
    src/black_hole_lensing_2d.cpp
)

target_link_libraries(black_hole_lensing_2d
    glfw3
    glew32
    opengl32
    gdi32
)