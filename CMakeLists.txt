# 1. Minimum version requirement for CMake
cmake_minimum_required(VERSION 3.20)

# 2. Project Name
project(VRDrummer)

# 3. Add the C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 4. Fetch OpenXR SDK
include(FetchContent)
FetchContent_Declare(
    openxr
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG        release-1.1.59.1 # Updated version
)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(openxr)

# 5. Fetch RtMidi
FetchContent_Declare(
    rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG        6.0.0 # Stable release
)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
# --- THE FIX ---
# Rename RtMidi's uninstall target so it doesn't collide with OpenXR's
set(RTMIDI_TARGETNAME_UNINSTALL "rtmidi_uninstall" CACHE STRING "" FORCE)
# --- THE FIX: Force Static Linking ---
# This overrides the default behavior and forces RtMidi to be 
# baked directly into VRDrummer.exe, eliminating the need for a .dll.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rtmidi)

# 6a. Add the executable target for the MIDI test application
add_executable(midiTest midiTest.cpp)
# 6b. Link the RtMidi libraries to the executable
target_link_libraries(midiTest PRIVATE rtmidi)

# 7a. Add the executable target for the VR test application
#add_executable(vrTest vrTest.cpp)
# 7b. Link the OpenXR loader to the executable
#target_link_libraries(vrTest PRIVATE openxr_loader)