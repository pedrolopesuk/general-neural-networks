cmake_minimum_required(VERSION 3.22)
project(MetaBrain VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /EHsc /permissive-)
    add_compile_options(
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Ot>
        $<$<CONFIG:Release>:/arch:AVX2>
        $<$<CONFIG:Release>:/fp:fast>
    )
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options(
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-ffast-math>
    )
endif()

add_library(metabrain_core STATIC
    src/Neuron.cpp
    src/SensoryPipeline.cpp
    src/BrainEngine.cpp
    src/Layout3D.cpp
    src/Serializer.cpp
    src/LiveServer.cpp
)

target_include_directories(metabrain_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(metabrain_core PUBLIC ws2_32)
endif()

add_executable(metabrain src/main.cpp)
target_link_libraries(metabrain PRIVATE metabrain_core)

add_executable(metabrain_tests tests/test_metabrain.cpp)
target_link_libraries(metabrain_tests PRIVATE metabrain_core)

enable_testing()
add_test(NAME MetaBrainUnitSuite COMMAND metabrain_tests)
