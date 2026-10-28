set(plMessageBox_SOURCES
    hsMessageBox.cpp
    $<$<PLATFORM_ID:Darwin>:hsMessageBox_Mac.mm>
    $<$<PLATFORM_ID:Windows>:hsMessageBox_Win.cpp>
    $<$<PLATFORM_ID:Emscripten>:hsMessageBox_Emscripten.cpp>
)

set(plMessageBox_HEADERS
    hsMessageBox.h
    $<$<PLATFORM_ID:Emscripten>:hsMessageBox_Emscripten.h>
    hsMessageBoxTestResponder.h
)

plasma_library(plMessageBox
    FOLDER PubUtilLib
    SOURCES ${plMessageBox_SOURCES} ${plMessageBox_HEADERS}
)
target_link_libraries(
    plMessageBox
    PUBLIC
        CoreLib
    PRIVATE
        "$<$<PLATFORM_ID:Darwin>:-framework Cocoa>"
)
# The page half of hsMessageBox_Emscripten.cpp
target_link_options(plMessageBox INTERFACE
    "$<$<PLATFORM_ID:Emscripten>:--js-library=${CMAKE_CURRENT_SOURCE_DIR}/hsMessageBox_Emscripten.js>"
)

source_group("Source Files" FILES ${plMessageBox_SOURCES})
source_group("Header Files" FILES ${plMessageBox_HEADERS})
