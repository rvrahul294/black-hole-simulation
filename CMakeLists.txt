cmake_minimum_required(VERSION 3.20)

project(BlackHole LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# Executable: 3D Black Hole Simulation & Ray Tracing (Compute Shader)
# -----------------------------------------------------------------------------
add_executable(black_hole_simulation_3d
    src/black_hole_simulation_3d.cpp
)

target_link_libraries(black_hole_simulation_3d PRIVATE
    glfw3
    glew32
    opengl32
    gdi32
)

# -----------------------------------------------------------------------------
# Executable: 2D Gravitational Lensing Simulation
# -----------------------------------------------------------------------------
add_executable(black_hole_lensing_2d
    src/black_hole_lensing_2d.cpp
)

target_link_libraries(black_hole_lensing_2d PRIVATE
    glfw3
    glew32
    opengl32
    gdi32
)

# -----------------------------------------------------------------------------
# Runtime Assets & DLL Deployment
# -----------------------------------------------------------------------------
# Copy shaders directory to build directory so executables can find them at runtime
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(
    TARGET black_hole_simulation_3d POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:black_hole_simulation_3d>/shaders
    COMMENT "Copying shaders to output directory"
)

# Copy glew32.dll to binary directory on Windows if present
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/glew32.dll")
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/glew32.dll DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

    add_custom_command(
        TARGET black_hole_simulation_3d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/glew32.dll
            $<TARGET_FILE_DIR:black_hole_simulation_3d>
        COMMENT "Copying glew32.dll to 3D executable directory"
    )

    add_custom_command(
        TARGET black_hole_lensing_2d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/glew32.dll
            $<TARGET_FILE_DIR:black_hole_lensing_2d>
        COMMENT "Copying glew32.dll to 2D executable directory"
    )
endif()