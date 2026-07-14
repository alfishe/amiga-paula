cmake_minimum_required(VERSION 3.20)
project(modplayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL2 REQUIRED CONFIG)

add_executable(modplayer
    src/main.cpp
    src/mod_loader.cpp
    src/replayer.cpp
    src/paula.cpp
    src/pwm_paula.cpp
    src/winuae_paula.cpp
    src/sinctable.cpp
    src/audio.cpp
    src/filters.cpp
)

target_include_directories(modplayer PRIVATE src)

if(TARGET SDL2::SDL2)
    target_link_libraries(modplayer PRIVATE SDL2::SDL2 SDL2::SDL2main)
else()
    target_include_directories(modplayer PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(modplayer PRIVATE ${SDL2_LIBRARIES})
endif()

if(APPLE)
    target_link_libraries(modplayer PRIVATE "-framework AudioToolbox")
endif()
