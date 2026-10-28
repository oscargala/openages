include_directories(${PLASMA_SOURCE_ROOT})
set(pfPython_SOURCES
    cyAccountManagement.cpp
    cyAnimation.cpp
    cyAvatar.cpp
    cyCamera.cpp
    cyDraw.cpp
    cyInputInterface.cpp
    cyMisc.cpp
    cyParticleSys.cpp
    cyPhysics.cpp
    cyPythonInterface.cpp
    cyPythonModule451.cpp
    plPythonFileMod.cpp
    plPythonPack.cpp
    plPythonSDLModifier.cpp
    pyAgeInfoStruct.cpp
    pyAgeLinkStruct.cpp
    pyAgeVault.cpp
    pyAlarm.cpp
    pyAudioControl.cpp
    pyCluster.cpp
    pyColor.cpp
    pyCritterBrain.cpp
    pyDniCoordinates.cpp
    pyDrawControl.cpp
    pyDynamicText.cpp
    pyGameCli.cpp
    pyGameScore.cpp
    pyGameScoreMsg.cpp
    pyGlueHelpers.cpp
    pyGmBlueSpiral.cpp
    pyGmMarker.cpp
    pyGmVarSync.cpp
    pyGrassShader.cpp
    pyGUIControl.cpp
    pyGUIControlButton.cpp
    pyGUIControlCheckBox.cpp
    pyGUIControlClickMap.cpp
    pyGUIControlDragBar.cpp
    pyGUIControlDraggable.cpp
    pyGUIControlDynamicText.cpp
    pyGUIControlEditBox.cpp
    pyGUIControlListBox.cpp
    pyGUIControlMultiLineEdit.cpp
    pyGUIControlRadioGroup.cpp
    pyGUIControlTextBox.cpp
    pyGUIControlValue.cpp
    pyGUIDialog.cpp
    pyGUIPopUpMenu.cpp
    pyGUISkin.cpp
    pyImage.cpp
    pyImageLibMod.cpp
    pyJournalBook.cpp
    pyKey.cpp
    pyKeyMap.cpp
    pyLayer.cpp
    pyMarkerMgr.cpp
    pyMatrix44.cpp
    pyMoviePlayer.cpp
    pyNetLinkingMgr.cpp
    pyNotify.cpp
    pyPlayer.cpp
    pySceneObject.cpp
    pySDL.cpp
    pySpawnPointInfo.cpp
    pyStatusLog.cpp
    pyStream.cpp
    pySwimCurrentInterface.cpp
    pyVault.cpp
    pyVaultAgeInfoListNode.cpp
    pyVaultAgeInfoNode.cpp
    pyVaultAgeLinkNode.cpp
    pyVaultChronicleNode.cpp
    pyVaultFolderNode.cpp
    pyVaultImageNode.cpp
    pyVaultMarkerGameNode.cpp
    pyVaultNode.cpp
    pyVaultNodeRef.cpp
    pyVaultPlayerInfoListNode.cpp
    pyVaultPlayerInfoNode.cpp
    pyVaultPlayerNode.cpp
    pyVaultSDLNode.cpp
    pyVaultSystemNode.cpp
    pyVaultTextNoteNode.cpp
    pyWaveSet.cpp
)

# The pak import hook and the Python 2.3 layer by ABI: CPython 3 keeps the PEP 451
# finder and exactly the H-uru source list (so its unity batches are unchanged); the py2 flavour
# (PLASMA_PYTHON_ABI=2) builds the PEP 302 finder, the C-API compat layer and CWE's enum types.
if(PLASMA_PYTHON_ABI STREQUAL "2")
    list(REMOVE_ITEM pfPython_SOURCES cyPythonModule451.cpp)
    list(APPEND pfPython_SOURCES cyPythonModule302.cpp pyEnum2.cpp pyPy2Compat.cpp pyPy2Text.cpp)
    # The CWE surface overlay (pyCWEOverlay2.h)
    list(APPEND pfPython_SOURCES pyCWEOverlay2.cpp)
    # CWE's synchronous ptScoreMgr and ptGameScore (pyScoreMgr2.h)
    list(APPEND pfPython_SOURCES pyScoreMgr2.cpp)
    # CWE's GameMgr game clients, messages and functions (pyGameMgr2.h)
    list(APPEND pfPython_SOURCES pyGameCli2.cpp pyGameMgrWire2.cpp pyGameMsg2.cpp)
endif()

set(pfPython_HEADERS
    cyAccountManagement.h
    cyAnimation.h
    cyAvatar.h
    cyCamera.h
    cyDraw.h
    cyInputInterface.h
    cyMisc.h
    cyParticleSys.h
    cyPhysics.h
    cyPythonInterface.h
    plPythonCallable.h
    plPythonConvert.h
    pfPythonCreatable.h
    plPythonFileMod.h
    plPythonHandlerArgs.h
    plPythonInitHooks.h
    plPythonPack.h
    plPythonParameter.h
    plPythonSDLModifier.h
    pyAgeInfoStruct.h
    pyAgeLinkStruct.h
    pyAgeVault.h
    pyAlarm.h
    pyAudioControl.h
    pyCluster.h
    pyColor.h
    pyCritterBrain.h
    pyDniCoordinates.h
    pyDrawControl.h
    pyDynamicText.h
    pyEnum.h
    pyGameCli.h
    pyGameHandler.h
    pyGameMgr.h
    pyGameScore.h
    pyGameScoreMsg.h
    pyGeometry3.h
    pyGmBlueSpiral.h
    pyGmMarker.h
    pyGmVarSync.h
    pyGrassShader.h
    pyGUIControl.h
    pyGUIControlButton.h
    pyGUIControlCheckBox.h
    pyGUIControlClickMap.h
    pyGUIControlDragBar.h
    pyGUIControlDraggable.h
    pyGUIControlDynamicText.h
    pyGUIControlEditBox.h
    pyGUIControlListBox.h
    pyGUIControlMultiLineEdit.h
    pyGUIControlRadioGroup.h
    pyGUIControlTextBox.h
    pyGUIControlValue.h
    pyGUIDialog.h
    pyGUIPopUpMenu.h
    pyGUISkin.h
    pyImage.h
    pyImageLibMod.h
    pyJournalBook.h
    pyKey.h
    pyKeyMap.h
    pyLayer.h
    pyMarkerMgr.h
    pyMatrix44.h
    pyMoviePlayer.h
    pyNetLinkingMgr.h
    pyNotify.h
    pyObjectRef.h
    pyPlayer.h
    pyPy2Compat.h
    pySceneObject.h
    pySDL.h
    pySpawnPointInfo.h
    pyStatusLog.h
    pyStream.h
    pySwimCurrentInterface.h
    pyVault.h
    pyVaultAgeInfoListNode.h
    pyVaultAgeInfoNode.h
    pyVaultAgeLinkNode.h
    pyVaultChronicleNode.h
    pyVaultFolderNode.h
    pyVaultImageNode.h
    pyVaultMarkerGameNode.h
    pyVaultNode.h
    pyVaultNodeRef.h
    pyVaultPlayerInfoListNode.h
    pyVaultPlayerInfoNode.h
    pyVaultPlayerNode.h
    pyVaultSDLNode.h
    pyVaultSystemNode.h
    pyVaultTextNoteNode.h
    pyWaveSet.h
)

# The headers of the py2 flavour's sources above.
if(PLASMA_PYTHON_ABI STREQUAL "2")
    list(APPEND pfPython_HEADERS
        pfPython2Creatable.h
        pyCWEOverlay2.h
        pyGameGlue2.h
        pyGameMgr2.h
        pyGameMgrWire2.h
        pyPy2Text.h
        pyScoreMgr2.h
    )
endif()

set(pfPython_GLUE
    cyAccountManagementGlue.cpp
    cyAnimationGlue.cpp
    cyAvatarGlue.cpp
    cyCameraGlue.cpp
    cyDrawGlue.cpp
    cyInputInterfaceGlue.cpp
    cyMiscGlue.cpp
    cyMiscGlue2.cpp
    cyMiscGlue3.cpp
    cyMiscGlue4.cpp
    cyParticleSysGlue.cpp
    cyPhysicsGlue.cpp
    plPythonSDLModifierGlue.cpp
    pyAgeInfoStructGlue.cpp
    pyAgeLinkStructGlue.cpp
    pyAgeVaultGlue.cpp
    pyAudioControlGlue.cpp
    pyClusterGlue.cpp
    pyColorGlue.cpp
    pyCritterBrainGlue.cpp
    pyDniCoordinatesGlue.cpp
    pyDrawControlGlue.cpp
    pyDynamicTextGlue.cpp
    pyGameCliGlue.cpp
    pyGameMgrGlue.cpp
    pyGameScoreGlue.cpp
    pyGameScoreMsgGlue.cpp
    pyGeometry3Glue.cpp
    pyGlueDefinitions.h
    pyGlueHelpers.h
    pyGmBlueSpiralGlue.cpp
    pyGmMarkerGlue.cpp
    pyGmVarSyncGlue.cpp
    pyGrassShaderGlue.cpp
    pyGUIControlButtonGlue.cpp
    pyGUIControlCheckBoxGlue.cpp
    pyGUIControlClickMapGlue.cpp
    pyGUIControlDragBarGlue.cpp
    pyGUIControlDraggableGlue.cpp
    pyGUIControlDynamicTextGlue.cpp
    pyGUIControlEditBoxGlue.cpp
    pyGUIControlGlue.cpp
    pyGUIControlListBoxGlue.cpp
    pyGUIControlMultiLineEditGlue.cpp
    pyGUIControlRadioGroupGlue.cpp
    pyGUIControlTextBoxGlue.cpp
    pyGUIControlValueGlue.cpp
    pyGUIDialogGlue.cpp
    pyGUIPopUpMenuGlue.cpp
    pyGUISkinGlue.cpp
    pyImageGlue.cpp
    pyImageLibModGlue.cpp
    pyJournalBookGlue.cpp
    pyKeyGlue.cpp
    pyKeyMapGlue.cpp
    pyLayerGlue.cpp
    pyMarkerMgrGlue.cpp
    pyMatrix44Glue.cpp
    pyMoviePlayerGlue.cpp
    pyNetLinkingMgrGlue.cpp
    pyNotifyGlue.cpp
    pyPlayerGlue.cpp
    pySceneObjectGlue.cpp
    pySDLGlue.cpp
    pySpawnPointInfoGlue.cpp
    pyStatusLogGlue.cpp
    pyStreamGlue.cpp
    pySwimCurrentInterfaceGlue.cpp
    pyVaultAgeInfoListNodeGlue.cpp
    pyVaultAgeInfoNodeGlue.cpp
    pyVaultAgeLinkNodeGlue.cpp
    pyVaultChronicleNodeGlue.cpp
    pyVaultFolderNodeGlue.cpp
    pyVaultGlue.cpp
    pyVaultImageNodeGlue.cpp
    pyVaultMarkerGameNodeGlue.cpp
    pyVaultNodeGlue.cpp
    pyVaultNodeRefGlue.cpp
    pyVaultPlayerInfoListNodeGlue.cpp
    pyVaultPlayerInfoNodeGlue.cpp
    pyVaultPlayerNodeGlue.cpp
    pyVaultSDLNodeGlue.cpp
    pyVaultSystemNodeGlue.cpp
    pyVaultTextNoteNodeGlue.cpp
    pyWaveSetGlue.cpp
)

plasma_library(pfPython
    FOLDER FeatureLib
    SOURCES ${pfPython_SOURCES} ${pfPython_HEADERS} ${pfPython_GLUE}
    UNITY_BUILD
    PRECOMPILED_HEADERS Pch.h
)
target_link_libraries(
    pfPython
    PUBLIC
        CoreLib
        pnNucleusInc
        plMessage
    PRIVATE
        pnEncryption
        pnInputCore
        pnKeyedObject
        pnMessage
        pnModifier
        pnNetCommon
        pnNucleusInc
        pnSceneObject
        pnUUID
        plAgeDescription
        plAnimation
        plAudio
        plAvatar
        plDrawable
        plFile
        plGImage
        plGLight
        plInputCore
        plSurface
        plModifier
        plNetClient
        plNetClientComm
        plNetCommon
        plNetTransport
        plPipeline
        plResMgr
        plSDL
        plStatusLog
        plSurface
        plVault
        pfCamera
        pfCharacter
        pfGameGUIMgr
        pfGameMgr
        pfGameScoreMgr
        pfJournalBook
        pfLocalizationMgr
        pfMessage
        ${PLASMA_PYTHON_TARGET}
    INTERFACE
        pnFactory
)

if(PLASMA_PYTHON_ABI STREQUAL "2")
    # pyScoreMgr2.cpp calls the auth score transactions and pumps the net client
    target_link_libraries(pfPython PRIVATE pnNetProtocol plNetGameLib)
endif()

if(EMSCRIPTEN AND PLASMA_PYTHON_ABI STREQUAL "3")
    # The static libpython3.12.a (wasm) needs its builtin modules' helper archives
    # (libmpdec, HACL SHA2, bz2, expat, zlib), set by cmake/emscripten/Toolchain.cmake.
    # libpython2.3.a (ABI 2) is self-contained.
    target_link_libraries(pfPython PRIVATE ${PLASMA_WASM_PYTHON_LINK_LIBS})
endif()

source_group("Source Files" FILES ${pfPython_SOURCES})
source_group("Header Files" FILES ${pfPython_HEADERS})
source_group("Glue Files" FILES ${pfPython_GLUE})
