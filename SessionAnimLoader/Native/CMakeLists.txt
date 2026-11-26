# SessionAnimLoaderNative: C++ half of Session Anim Loader (UE4SS 3.0.1). Produces dlls/main.dll.
#   lib /def:third_party/ue4ss/UE4SS.def /out:third_party/ue4ss/UE4SS.lib /machine:x64
#   cmake -S . -B <build> -G "Visual Studio 17 2022" -A x64
#   cmake --build <build> --config Release
# asn_core: the pure logic (src/), without UE4SS; asn_tests: its tests outside the game (test.cmd).
cmake_minimum_required(VERSION 3.20)
project(SessionAnimLoaderNative CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")   # UE4SS is /MD and passes us std:: objects: same CRT

file(GLOB ASN_CORE_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.h)
add_library(asn_core STATIC ${ASN_CORE_SRC})
set_target_properties(asn_core PROPERTIES LINKER_LANGUAGE CXX)
target_compile_definitions(asn_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(asn_core PUBLIC /W3 /O2 /EHsc /utf-8 /wd4251)

add_library(SessionAnimLoaderNative SHARED main.cpp)
set_target_properties(SessionAnimLoaderNative PROPERTIES OUTPUT_NAME main)
target_include_directories(SessionAnimLoaderNative PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(SessionAnimLoaderNative PRIVATE asn_core ${CMAKE_SOURCE_DIR}/third_party/ue4ss/UE4SS.lib)

file(GLOB ASN_TEST_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
add_executable(asn_tests ${ASN_TEST_SRC})
# the editor (SessionStudio) shares asn_core; its logic without interface (asn_editor) is tested here
add_subdirectory(${CMAKE_SOURCE_DIR}/../../SessionStudio ${CMAKE_BINARY_DIR}/editor)
target_link_libraries(asn_tests PRIVATE asn_core asn_editor)
