set(pfPatcher_SOURCES
    plManifests.cpp
    pfPatcher.cpp
)

set(pfPatcher_HEADERS
    plManifests.h
    pfPatcher.h
)

plasma_library(pfPatcher
    FOLDER FeatureLib
    SOURCES ${pfPatcher_SOURCES} ${pfPatcher_HEADERS}
)
target_link_libraries(
    pfPatcher
    PUBLIC
        CoreLib
        pnNetBase
    PRIVATE
        pnEncryption
        pnNucleusInc
        plAudioCore
        plCompression
        plNetGameLib
        plStatusLog
)
if(EMSCRIPTEN)
    # the browser's size check reads the inflater's counts (pfPatcherStream::Close), so pfPatcher.cpp includes zlib.h
    target_link_libraries(pfPatcher PRIVATE ZLIB::ZLIB)
    # the browser's download check (pfPatcherStream), which takes the CRC-32 of each chunk at its write
    target_sources(pfPatcher PRIVATE pfPatcherDownloadCheck.h)
endif()

source_group("Source Files" FILES ${pfPatcher_SOURCES})
source_group("Header Files" FILES ${pfPatcher_HEADERS})
