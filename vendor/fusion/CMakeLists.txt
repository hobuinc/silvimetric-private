cmake_minimum_required(VERSION 3.20)

# The upstream GridMetrics project is an MFC Visual Studio project.  This
# target deliberately builds only the command-line utility and supplies a
# small source-compatible MFC/Win32 compatibility layer for the code paths
# GridMetrics uses.
project(fusion_gridmetrics LANGUAGES C CXX)

include(CTest)

add_executable(GridMetrics
    Common/FUSION_util/COPCIndex.cpp
    Common/FUSION_util/SpatialExtent.cpp
    Common/FUSION_util/argslib.cpp
    Common/FUSION_util/DataFile.cpp
    Common/FUSION_util/DataIndex.cpp
    Common/FUSION_util/filespec.cpp
    Common/FUSION_util/LASFormatFile.cpp
    Common/FUSION_util/LidarData_LASlib.cpp
    Common/FUSION_util/plansdtm.cpp
    Common/TerrainSet/TerrainSet.cpp
    Common/LAStools/laszip_api.c
    FUSION_Metrics/GridMetrics/GridMetrics.cpp
    FUSION_Metrics/GridMetrics/StdAfx.cpp
)

target_compile_features(GridMetrics PRIVATE cxx_std_17)
target_compile_definitions(GridMetrics PRIVATE
    FUSION_PORTABLE=1
    _tmain=main
    _CRT_SECURE_NO_WARNINGS=1
    _M_X64=1
)
target_include_directories(GridMetrics PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/portable/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Common/FUSION_util
    ${CMAKE_CURRENT_SOURCE_DIR}/Common/LTK
    ${CMAKE_CURRENT_SOURCE_DIR}/Common/array2d
    ${CMAKE_CURRENT_SOURCE_DIR}/Common/LAStools
    ${CMAKE_CURRENT_SOURCE_DIR}/Common/TerrainSet
    ${CMAKE_CURRENT_SOURCE_DIR}/FUSION_Metrics/GridMetrics
)

if(UNIX)
    target_link_libraries(GridMetrics PRIVATE dl)
endif()

install(TARGETS GridMetrics RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    add_test(NAME GridMetrics.version COMMAND GridMetrics /version)
endif()
