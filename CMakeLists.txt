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