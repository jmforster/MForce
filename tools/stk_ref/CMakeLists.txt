cmake_minimum_required(VERSION 3.16)
project(stk_ref CXX)
set(CMAKE_CXX_STANDARD 11)

# STK checkout lives as a sibling of the mforce repo.
set(STK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../stk")

add_executable(stk_bowed_ref
    bowed_ref.cpp
    ${STK_ROOT}/src/Stk.cpp
    ${STK_ROOT}/src/Bowed.cpp
    ${STK_ROOT}/src/ADSR.cpp
    ${STK_ROOT}/src/BiQuad.cpp
    ${STK_ROOT}/src/DelayL.cpp
    ${STK_ROOT}/src/OnePole.cpp
    ${STK_ROOT}/src/SineWave.cpp
)
target_include_directories(stk_bowed_ref PRIVATE ${STK_ROOT}/include)

add_executable(stk_clarinet_ref
    clarinet_ref.cpp
    ${STK_ROOT}/src/Stk.cpp
    ${STK_ROOT}/src/Clarinet.cpp
    ${STK_ROOT}/src/Envelope.cpp
    ${STK_ROOT}/src/Noise.cpp
    ${STK_ROOT}/src/OneZero.cpp
    ${STK_ROOT}/src/DelayL.cpp
    ${STK_ROOT}/src/SineWave.cpp
)
target_include_directories(stk_clarinet_ref PRIVATE ${STK_ROOT}/include)

add_executable(stk_flute_ref
    flute_ref.cpp
    ${STK_ROOT}/src/Stk.cpp
    ${STK_ROOT}/src/Flute.cpp
    ${STK_ROOT}/src/ADSR.cpp
    ${STK_ROOT}/src/Noise.cpp
    ${STK_ROOT}/src/OnePole.cpp
    ${STK_ROOT}/src/PoleZero.cpp
    ${STK_ROOT}/src/DelayL.cpp
    ${STK_ROOT}/src/SineWave.cpp
)
target_include_directories(stk_flute_ref PRIVATE ${STK_ROOT}/include)

add_executable(stk_brass_ref
    brass_ref.cpp
    ${STK_ROOT}/src/Stk.cpp
    ${STK_ROOT}/src/Brass.cpp
    ${STK_ROOT}/src/ADSR.cpp
    ${STK_ROOT}/src/BiQuad.cpp
    ${STK_ROOT}/src/PoleZero.cpp
    ${STK_ROOT}/src/DelayA.cpp
    ${STK_ROOT}/src/SineWave.cpp
)
target_include_directories(stk_brass_ref PRIVATE ${STK_ROOT}/include)

foreach(tgt stk_bowed_ref stk_clarinet_ref stk_flute_ref stk_brass_ref)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /O2)
        target_compile_definitions(${tgt} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endforeach()
