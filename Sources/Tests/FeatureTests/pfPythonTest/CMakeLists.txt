set(pfPythonTest_SOURCES
    test_cyMisc.cpp
)
# test_plPythonFileMod.cpp runs script handlers on the CPython 3 the py3 flavour links.
if(NOT PLASMA_PYTHON_ABI STREQUAL "2")
    list(APPEND pfPythonTest_SOURCES test_plPythonFileMod.cpp)
endif()

plasma_test(test_pfPython SOURCES ${pfPythonTest_SOURCES})
target_link_libraries(
    test_pfPython
    PRIVATE
        CoreLib
        pnNucleusInc
        plPubUtilInc
        pfFeatureInc
        pfPython
        ${PLASMA_PYTHON_TARGET}
        gtest_main
)
# test_plPythonFileMod.cpp starts an interpreter on the standard library of the Python found above.
if(Python3_STDLIB)
    target_compile_definitions(test_pfPython PRIVATE "PLASMA_TEST_PYTHON_STDLIB=\"${Python3_STDLIB}\"")
endif()

# The py2 flavour's units that hold no Python (the text codec and the GameMgr wire), built from
# their sources so that they are tested with either Python ABI. pfGmHeek.cpp is in pfGameMgr only
# for ABI 2.
set(pfPython2Test_SOURCES
    test_pyGameMgrWire2.cpp
    test_pyPy2Text.cpp
    ${PLASMA_SOURCE_ROOT}/FeatureLib/pfPython/pyGameMgrWire2.cpp
    ${PLASMA_SOURCE_ROOT}/FeatureLib/pfPython/pyPy2Text.cpp
)
if(NOT PLASMA_PYTHON_ABI STREQUAL "2")
    list(APPEND pfPython2Test_SOURCES ${PLASMA_SOURCE_ROOT}/FeatureLib/pfGameMgr/pfGmHeek.cpp)
endif()

plasma_test(test_pfPython2 SOURCES ${pfPython2Test_SOURCES})
target_link_libraries(
    test_pfPython2
    PRIVATE
        CoreLib
        pnGameMgr
        pnNucleusInc
        pnUtils
        plNetGameLib
        plPubUtilInc
        pfAnimation
        pfAudio
        pfCamera
        pfConditional
        pfGameMgr
        pfMessage
        gtest_main
)
