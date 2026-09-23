cmake_minimum_required(VERSION 3.21)
project(AllowgramSheetTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
find_package(Qt6 REQUIRED COMPONENTS Core Network)
get_filename_component(src "${CMAKE_CURRENT_LIST_DIR}/../../../SourceFiles" ABSOLUTE)
qt_add_executable(allowlist_sheet_test
    "${src}/test/allowlist_sheet_test.cpp"
    "${src}/main/allowlist_sheet.cpp"
    "${src}/main/allowlist_sheet_resolver.cpp"
    "${src}/main/allowlist_policy.cpp")
target_include_directories(allowlist_sheet_test PRIVATE "${src}")
target_compile_definitions(allowlist_sheet_test PRIVATE QT_NO_DEBUG)
target_link_libraries(allowlist_sheet_test PRIVATE Qt6::Core Qt6::Network)
if (MSVC)
    set_property(TARGET allowlist_sheet_test PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
    target_compile_options(allowlist_sheet_test PRIVATE /utf-8 /Zc:__cplusplus /permissive-)
endif()
enable_testing()
add_test(NAME allowlist_sheet_protocol COMMAND allowlist_sheet_test)
