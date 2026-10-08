cmake_minimum_required(VERSION 3.16)

# set the project name
project(arduino-smb LANGUAGES C CXX)

option(SMB_BUILD_EXAMPLES "Build the desktop examples" ON)
option(SMB_BUILD_TESTS "Build the tests (run with ctest, the end to end test needs smbclient)" ON)

set(CMAKE_CXX_STANDARD 17)

# Arduino API on the desktop. Use a local checkout with
#   -DFETCHCONTENT_SOURCE_DIR_ARDUINO_EMULATOR=/path/to/Arduino-Emulator
if(NOT TARGET arduino_emulator)
    include(FetchContent)
    FetchContent_Declare(arduino_emulator GIT_REPOSITORY "https://github.com/pschatzmann/Arduino-Emulator.git" GIT_TAG main )
    FetchContent_GetProperties(arduino_emulator)
    if(NOT arduino_emulator_POPULATED)
        FetchContent_Populate(arduino_emulator)
        add_subdirectory(${arduino_emulator_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/emulator)
    endif()
endif()

# don't report warnings of the emulator headers
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
    set_target_properties(arduino_emulator PROPERTIES SYSTEM TRUE)
endif()

# Header-only library
add_library(arduino-smb INTERFACE)
# USE_FILESYSTEM: directory listing in the emulated SD library
target_compile_definitions(arduino-smb INTERFACE IS_DESKTOP USE_FILESYSTEM)
target_include_directories(arduino-smb INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include/arduino-smb>
)
target_link_libraries(arduino-smb INTERFACE arduino_emulator)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/"
    DESTINATION "include/arduino-smb"
    FILES_MATCHING PATTERN "*.h"
)

# Adds an executable from an Arduino sketch
function(smb_sketch name ino_file)
    set_source_files_properties(${ino_file} PROPERTIES LANGUAGE CXX)
    add_executable(${name} ${ino_file})
    target_compile_options(${name} PRIVATE -x c++ -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(${name} PRIVATE arduino-smb)
endfunction()

if(SMB_BUILD_EXAMPLES)
    add_subdirectory(examples/desktop)
endif()

if(SMB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
