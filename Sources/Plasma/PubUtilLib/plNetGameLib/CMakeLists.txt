set(plNetGameLib_HEADERS
    Intern.h
    plNetGameLib.h
    plNglAuth.h
    plNglCore.h
    plNglFile.h
    plNglGame.h
    plNglGateKeeper.h
)

set(plNetGameLib_SOURCES
    plNglAuth.cpp
    plNglCore.cpp
    plNglFile.cpp
    plNglGame.cpp
    plNglGateKeeper.cpp
    plNglMisc.cpp
    plNglTrans.cpp
)

# NOTE: Too many ODR violations and ambiguous names for UNITY_BUILD on this one.
plasma_library(plNetGameLib
    FOLDER PubUtilLib
    SOURCES ${plNetGameLib_HEADERS} ${plNetGameLib_SOURCES}
    PRECOMPILED_HEADERS Pch.h
)

target_link_libraries(
    plNetGameLib
    PUBLIC
        CoreLib
        pnEncryption
        pnNetBase
    PRIVATE
        pnAsyncCore
        pnGameMgr
        pnNetBase
        pnNetCli
        pnNetCommon
        pnNetProtocol
        pnUtils
        pnUUID
        plVault
)
if(EMSCRIPTEN)
    # the browser's download check takes each file chunk's CRC-32 where it arrives (DownloadRequestTrans::Recv), so
    # plNglFile.cpp includes zlib.h
    target_link_libraries(plNetGameLib PRIVATE ZLIB::ZLIB)
endif()

source_group("Header Files" FILES ${plNetGameLib_HEADERS})
source_group("Source Files" FILES ${plNetGameLib_SOURCES})
