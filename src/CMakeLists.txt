cmake_minimum_required(VERSION 3.16)
project(whisper-hotkeyd VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(KF6GlobalAccel REQUIRED)

add_executable(whisper-hotkeyd whisper-hotkeyd.cpp)

target_compile_options(whisper-hotkeyd PRIVATE -Wall -Wextra)
target_link_libraries(whisper-hotkeyd PRIVATE Qt6::Widgets KF6::GlobalAccel)

install(TARGETS whisper-hotkeyd RUNTIME DESTINATION bin)
install(FILES whisper-hotkeyd.desktop DESTINATION share/applications)