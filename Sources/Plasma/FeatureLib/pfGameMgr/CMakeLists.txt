set(pfGameMgr_HEADERS
    pfGameCli.h
    pfGameMgr.h
    pfGameMgrCreatable.h
    pfGameMgrTrans.h
    pfGmBlueSpiral.h
    pfGmMarker.h
    pfGmVarSync.h
)

set(pfGameMgr_SOURCES
    pfGameCli.cpp
    pfGameMgr.cpp
    pfGameMgrTrans.cpp
    pfGmBlueSpiral.cpp
    pfGmMarker.cpp
    pfGmVarSync.cpp
)

# The Heek client (pfGmHeek.h), which only the py2 flavour's MOULa scripts play
# through the GameMgr. The py3 flavour keeps exactly H-uru's source list.
if(PLASMA_PYTHON_ABI STREQUAL "2")
    list(APPEND pfGameMgr_HEADERS pfGameMgr2Creatable.h pfGmHeek.h)
    list(APPEND pfGameMgr_SOURCES pfGmHeek.cpp)
endif()

plasma_library(pfGameMgr
    FOLDER FeatureLib
    SOURCES ${pfGameMgr_HEADERS} ${pfGameMgr_SOURCES}
)
target_link_libraries(
    pfGameMgr
    PUBLIC
        CoreLib
        pnFactory
        pnGameMgr
    PRIVATE
        pnUtils
        pnUUID
        plNetClientComm
        plNetGameLib
)

source_group("Header Files" FILES ${pfGameMgr_HEADERS})
source_group("Source Files" FILES {pfGameMgr_SOURCES})
