cmake_minimum_required(VERSION 3.20)
project(VRDrummer)

# --- 1. Fetch OpenXR SDK ---
include(FetchContent)
FetchContent_Declare(
    openxr
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG        release-1.1.59.1 # Updated version
)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(openxr)

# --- 2. Fetch RtMidi ---
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

# --- 3. Our Application ---
add_executable(VRDrummer main.cpp)

# --- 4. Linkage ---
# We now link BOTH the openxr_loader and rtmidi libraries
target_link_libraries(VRDrummer PRIVATE openxr_loader rtmidi)