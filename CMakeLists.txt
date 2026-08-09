add_library(emper-backend-sdl3
    sdl3/Renderer.cpp
)

target_include_directories(emper-backend-sdl3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl3
)

target_link_libraries(emper-backend-sdl3 PUBLIC
    emper-engine
)

find_package(SDL3 REQUIRED)
target_link_libraries(emper-backend-sdl3 PUBLIC
    SDL3::SDL3
)

add_library(emper-backend-opengl
    opengl/OpenGLComputeBackend.cpp
    opengl/SDLOpenGLRenderer.cpp
)

target_include_directories(emper-backend-opengl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/opengl
    ${CMAKE_CURRENT_SOURCE_DIR}/glad/include
)

find_package(OpenGL REQUIRED)

add_library(glad STATIC
    glad/src/gl.c
)

target_include_directories(glad
    PUBLIC
        glad/include
)

target_link_libraries(emper-backend-opengl PUBLIC
    OpenGL::GL
    SDL3::SDL3
    glad
    emper-engine
)