# arduino-fatfs for the FatFs share of the test server. Use a local checkout
# with -DFETCHCONTENT_SOURCE_DIR_ARDUINO_FATFS=/path/to/arduino-fatfs
include(FetchContent)
FetchContent_Declare(arduino_fatfs GIT_REPOSITORY "https://github.com/pschatzmann/arduino-fatfs.git" GIT_TAG main)
FetchContent_GetProperties(arduino_fatfs)
if(NOT arduino_fatfs_POPULATED)
    FetchContent_Populate(arduino_fatfs)
endif()

smb_sketch(smb-test smb-test/smb-test.ino)
# SYSTEM: don't report warnings of the FatFs sources
target_include_directories(smb-test SYSTEM PRIVATE ${arduino_fatfs_SOURCE_DIR}/src)

add_executable(crypto-test crypto-test/crypto-test.cpp)
target_compile_options(crypto-test PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(crypto-test PRIVATE arduino-smb)
add_test(NAME smb-crypto COMMAND crypto-test)

find_program(SMBCLIENT smbclient)
if(SMBCLIENT)
    add_test(NAME smb-smbclient
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/smbclient-test.sh $<TARGET_FILE:smb-test>
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
else()
    message(STATUS "smbclient not found: smb-smbclient test disabled")
endif()

smb_sketch(smb-client-test smb-client-test/smb-client-test.ino)
add_test(NAME smb-client
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/client-test.sh $<TARGET_FILE:smb-test> $<TARGET_FILE:smb-client-test>
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Optional: test the client against Samba in a Docker container
option(SMB_TEST_SAMBA "Test the client against Samba (needs Docker and internet access)" OFF)
if(SMB_TEST_SAMBA)
    add_test(NAME smb-client-samba
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/samba-test.sh $<TARGET_FILE:smb-client-test>
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

add_executable(pending-test pending-test/pending-test.cpp)
target_compile_options(pending-test PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(pending-test PRIVATE arduino-smb)
add_test(NAME smb-client-pending COMMAND pending-test)
