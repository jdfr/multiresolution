CMAKE_MINIMUM_REQUIRED(VERSION 2.8.0)

option(ENABLE_SLICER_LOGGING "enable slicer debug output to file" ON) #this option has to be coordinated with the one used in the slicer!!!!
option(USE_PYTHON_DEBUG      "add support for viewing contours in python for debugging" ON)
option(USE_PYTHON_VIEWER     "add support for viewing slices in python" ON)
option(GENERATE_LIBRARY      "generate shared library"  ON)
option(GENERATE_EXECUTABLE   "generate standalone executable"  ON)
option(SET_CONFIGURE_FILE    "generate example configuration files (the variables PYPATH and INITIAL_MESH_FILE must be set!!!)" ON)
option(GENERATE_SVGCONVERTER "generate svg converter executable"  ON)
option(GENERATE_XYZHANDLER   "generate utility to process xyz point cloud files"  ON)
option(GENERATE_TRANSFORMER  "generate executable to apply transformations to paths"  ON)
option(GENERATE_FILEFILTER   "generate filter for paths files"  ON)
option(GENERATE_FILEINFO     "generate info dumper for paths files"  ON)
option(GENERATE_FILEUNION    "generate tool to merge several paths files into one"  ON)
option(GENERATE_FILETOUCH    "generate slice header setter for paths files"  ON)
option(COMPILE_OPTIMIZATIONS "optimizations that may make the build process significantly slower" ON)
option(LINKING_OPTIMIZATION  "optimizations that may make the build process significantly slower" OFF)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR})

#if present, include code from this file. Useful for setting some configuration values such as PYPATH
include(${SRC}/cmake.configvars.txt OPTIONAL)

if (USE_PYTHON_DEBUG OR USE_PYTHON_VIEWER)
  set(USE_PYTHON ON)
else()
  set(USE_PYTHON OFF)
endif()

if(NOT GENERATE_LIBRARY AND NOT GENERATE_EXECUTABLE)
  message(FATAL_ERROR "PLEASE SET ON AT LEAST ONE OF THESE FLAGS: GENERATE_LIBRARY, GENERATE_EXECUTABLE")
endif()

if (USE_PYTHON OR GENERATE_EXECUTABLE)
  if (USE_PYTHON AND NOT GENERATE_EXECUTABLE)
    set(MSG "FOR THE PYTHON CALLER CODE!!!")
  endif()
  if (NOT USE_PYTHON AND GENERATE_EXECUTABLE)
    set(MSG "FOR THE EXTERNAL SLICER MANAGER!!!")
  endif()
  if (USE_PYTHON AND GENERATE_EXECUTABLE)
    set(MSG "FOR THE EXTERNAL SLICER MANAGER AND THE PYTHON CALLER CODE!!!")
  endif()
  message("PLEASE MAKE SURE TO USE A CONFIGURATION FILE WITH CORRECT DEFINITIONS ${MSG}")
endif()

if(EXISTS ${SRC}/../clipper)
  set(CLIPPER_BASE_DIR ${SRC}/../clipper)
else()
  #as far as I understand, this will enable users of cmake-gui to set this directory
  set(CLIPPER_BASE_DIR "" CACHE PATH "path to clipper library")
endif()

if(EXISTS ${SRC}/../boost/include/boost)
  set(BOOST_INCLUDE_DIR ${SRC}/../boost/include)
else()
  #as far as I understand, this will enable users of cmake-gui to set this directory
  set(BOOST_INCLUDE_DIR "" CACHE PATH "path to boost library")
endif()

set(clipper_include_dir ${CLIPPER_BASE_DIR}/clipper)
set(iopaths_include_dir ${CLIPPER_BASE_DIR}/iopaths)
set(clipper_source ${clipper_include_dir}/clipper.cpp)
set(clipper_header ${clipper_include_dir}/clipper.hpp)
set(iopaths_source ${iopaths_include_dir}/iopaths.cpp)
set(iopaths_header ${iopaths_include_dir}/iopaths.hpp)
set(common_header  ${iopaths_include_dir}/common.hpp)

if(NOT EXISTS ${clipper_source} OR NOT EXISTS ${iopaths_source} OR NOT EXISTS ${clipper_header} OR NOT EXISTS ${iopaths_header} OR NOT EXISTS ${common_header})
  message(FATAL_ERROR "tweaked clipper/iopaths base dir does not contain the expected files and directories!!!")
endif()

PROJECT(multiresolution)

if(NOT(CMAKE_SIZEOF_VOID_P EQUAL 8))
  message(FATAL_ERROR "This code is designed only for x64 systems! Size of void pointer: " ${CMAKE_SIZEOF_VOID_P})
endif()

if (NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE "Release" CACHE STRING "Release type")
  #SET(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Release type")
endif()

ADD_DEFINITIONS(-DCLIPPER_USE_INTRINSIC_128)
if (MSVC)
  #_CRT_SECURE_NO_WARNINGS: disable annoying warnings
  #BOOST_ALL_NO_LIB: disable autolink feature in BOOST causing trouble because we are brewing outside of BOOST infrastructure just what we need
  ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS -DBOOST_ALL_NO_LIB)
  if (FALSE)
    #The high amount of warnings while building with this block enabled
    #suggests that I currently do not know how to properly pass optimization
    #flags to visual studio with cmake. Anyway, the output is reasonably
    #fast, unlike non-explicitly optimized GCC
    if(COMPILE_OPTIMIZATIONS)
      set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} " /fp:fast /Ox /GF Gy /GS- ")
    endif()
    if(LINKING_OPTIMIZATION)
      set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} /GL)
      set(CMAKE_EXE_LINKER_FLAGS    ${CMAKE_EXE_LINKER_FLAGS}    " /GL ")
      set(CMAKE_SHARED_LINKER_FLAGS ${CMAKE_SHARED_LINKER_FLAGS} " /GL ")
    endif()
  endif()
endif()

if (UNIX AND CMAKE_COMPILER_IS_GNUCC)
  #no need to check GCC version, we already require it to compile c++11 anyway!
  #execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion OUTPUT_VARIABLE GCC_VERSION)
  #if (GCC_VERSION VERSION_GREATER 4.0 OR GCC_VERSION VERSION_EQUAL 4.0)
    ADD_DEFINITIONS(-fvisibility=hidden)
  #endif()
endif()

if(MINGW OR CMAKE_COMPILER_IS_GNUCC)
  ADD_DEFINITIONS(-std=c++11)
  if(COMPILE_OPTIMIZATIONS)
    ADD_DEFINITIONS(-Ofast -ffast-math -march=native)
  endif()
  if(LINKING_OPTIMIZATION)
    ADD_DEFINITIONS(-fwhole-program -flto)
    set(CMAKE_EXE_LINKER_FLAGS    ${CMAKE_EXE_LINKER_FLAGS}    -flto)
    set(CMAKE_SHARED_LINKER_FLAGS ${CMAKE_SHARED_LINKER_FLAGS} -flto)
  endif()
endif()

set (INCLUDE_DIRS
  ${clipper_include_dir}
  ${iopaths_include_dir}
  ${BOOST_INCLUDE_DIR}
  ${SRC}/multi
  ${SRC}/interfaces
  ${SRC}/python
  ${SRC}/shared
  ${SRC}/apps
  )
include_directories(${INCLUDE_DIRS})

set(CORESOURCES
  ${clipper_header}
  ${clipper_source}
  ${iopaths_header}
  ${iopaths_source}
  ${common_header}
  multi/3d.hpp
  multi/3d.cpp
  multi/auxgeom.hpp
  multi/auxgeom.cpp
  multi/config.hpp
  multi/config.cpp
  multi/medialaxis.hpp
  multi/medialaxis.cpp
  multi/motionPlanner.hpp
  multi/motionPlanner.cpp
  multi/multislicer.hpp
  multi/multislicer.cpp
  multi/orientPaths.hpp
  multi/orientPaths.cpp
  multi/snapToGrid.hpp
  multi/snapToGrid.cpp
  multi/spec.hpp
  multi/spec.cpp
  interfaces/simpleparsing.hpp
  interfaces/simpleparsing.cpp
  interfaces/parsing.hpp
  interfaces/parsing.cpp
  interfaces/pathsfile.hpp
  interfaces/pathsfile.cpp
  interfaces/pathwriter.hpp
  interfaces/pathwriter.cpp
  interfaces/slicermanager.hpp
  interfaces/slicermanager.cpp
  interfaces/subprocess.hpp
  interfaces/subprocess.cpp
  )

if (USE_PYTHON_DEBUG)
  set(CORESOURCES
    ${CORESOURCES}
    python/showcontours.hpp
    python/showcontours.cpp
    )
endif()

if(EXISTS ${SRC}/../boost/program_options/src)
  set(BOOST_PROGRAMOPTIONS_DIR ${SRC}/../boost/program_options/src)
else()
  #as far as I understand, this will enable users of cmake-gui to set this directory
  set(BOOST_PROGRAMOPTIONS_DIR "" CACHE PATH "path to boost::program_options source files")
endif()

set(BOOSTSOURCES
    ${BOOST_PROGRAMOPTIONS_DIR}/cmdline.cpp
    ${BOOST_PROGRAMOPTIONS_DIR}/config_file.cpp
    ${BOOST_PROGRAMOPTIONS_DIR}/options_description.cpp
    ${BOOST_PROGRAMOPTIONS_DIR}/parsers.cpp
    ${BOOST_PROGRAMOPTIONS_DIR}/variables_map.cpp
    ${BOOST_PROGRAMOPTIONS_DIR}/value_semantic.cpp
    ${BOOST_PROGRAMOPTIONS_DIR}/positional_options.cpp
    ${BOOST_PROGRAMOPTIONS_DIR}/utf8_codecvt_facet.cpp
    ${BOOST_PROGRAMOPTIONS_DIR}/convert.cpp
    ${BOOST_PROGRAMOPTIONS_DIR}/winmain.cpp
    ${BOOST_PROGRAMOPTIONS_DIR}/split.cpp
    )

set(CORESOURCES ${CORESOURCES})

if (NOT LINKING_OPTIMIZATION)
  ADD_LIBRARY(corelib      STATIC ${CORESOURCES})
  ADD_LIBRARY(miniboostlib STATIC ${BOOSTSOURCES})
  if (ENABLE_SLICER_LOGGING)
      set_property(TARGET corelib APPEND PROPERTY COMPILE_DEFINITIONS "SLICER_USE_DEBUG_FILE")
  endif()
  if ((NOT WIN32) AND CMAKE_COMPILER_IS_GNUCC)
    target_compile_options(corelib      PUBLIC "-fPIC")
    target_compile_options(miniboostlib PUBLIC "-fPIC" "-Wno-deprecated-declarations")
  endif()
endif()
  
if(GENERATE_LIBRARY)
  set(LIBRARY_SOURCES
    shared/multiresolution.h
    shared/multiresolution.cpp
    )
  if(LINKING_OPTIMIZATION)
    set(LIBRARY_SOURCES
      ${LIBRARY_SOURCES}
      ${CORESOURCES}
      ${BOOSTSOURCES}
      )
  endif()
  ADD_LIBRARY(multiresolution_library SHARED ${LIBRARY_SOURCES})
  set_target_properties(multiresolution_library PROPERTIES COMPILE_DEFINITIONS "LIBRARY_EXPORTS")
  set_target_properties(multiresolution_library PROPERTIES OUTPUT_NAME multires)
  if(LINKING_OPTIMIZATION)
    target_compile_options(multiresolution_library PUBLIC "-Wno-deprecated-declarations")
  else()
    target_link_libraries(multiresolution_library corelib miniboostlib)
  endif()
endif()
  
if(GENERATE_EXECUTABLE)
  set(EXECSOURCES
    apps/standalone.cpp
    )
  if(USE_PYTHON_VIEWER)
    set(EXECSOURCES
      ${EXECSOURCES}
      python/sliceviewer.hpp
      python/sliceviewer.cpp
      )
  endif()
  if(LINKING_OPTIMIZATION)
    set(EXECSOURCES
      ${EXECSOURCES}
      ${CORESOURCES}
      ${BOOSTSOURCES}
      )
  endif()
  ADD_EXECUTABLE(multiresolution_executable ${EXECSOURCES})
  set_target_properties(multiresolution_executable PROPERTIES OUTPUT_NAME multires)
  if (USE_PYTHON_DEBUG AND USE_PYTHON_VIEWER)
    set_property(TARGET multiresolution_executable APPEND PROPERTY COMPILE_DEFINITIONS "STANDALONE_USEPYTHON")
  endif()
  if(LINKING_OPTIMIZATION)
    target_compile_options(multiresolution_executable PUBLIC "-Wno-deprecated-declarations")
  else()
    target_link_libraries(multiresolution_executable corelib miniboostlib)
  endif()
endif()

function(SINGLESOURCE_EXECUTABLE GENERATE_OPTION TARGETNAME SOURCEFILE EXENAME)
  if(GENERATE_OPTION)
    set(SINGLESOURCE ${SOURCEFILE})
    if(LINKING_OPTIMIZATION)
      set(SINGLESOURCE ${SINGLESOURCE} ${CORESOURCES} ${BOOSTSOURCES})
    endif()
    ADD_EXECUTABLE(${TARGETNAME} ${SINGLESOURCE})
    set_target_properties(${TARGETNAME} PROPERTIES OUTPUT_NAME ${EXENAME})
    if(LINKING_OPTIMIZATION)
      target_compile_options(${TARGETNAME} PUBLIC "-Wno-deprecated-declarations")
    else()
      target_link_libraries(${TARGETNAME} corelib miniboostlib)
    endif()
  endif()
endfunction(SINGLESOURCE_EXECUTABLE)

SINGLESOURCE_EXECUTABLE(GENERATE_SVGCONVERTER, multiresolution_svgconverter apps/svg.cpp       svgconv)
SINGLESOURCE_EXECUTABLE(GENERATE_SVGCONVERTER, multiresolution_dxfconverter apps/dxf.cpp       dxfconv)
SINGLESOURCE_EXECUTABLE(GENERATE_FILEFILTER,   multiresolution_filefilter   apps/filter.cpp    filterp)
SINGLESOURCE_EXECUTABLE(GENERATE_FILEINFO,     multiresolution_fileinfo     apps/info.cpp      infop)
SINGLESOURCE_EXECUTABLE(GENERATE_FILEUNION,    multiresolution_fileunion    apps/union.cpp     unionp)
SINGLESOURCE_EXECUTABLE(GENERATE_FILETOUCH,    multiresolution_filetouch    apps/touch.cpp     touchp)
SINGLESOURCE_EXECUTABLE(GENERATE_TRANSFORMER,  multiresolution_transform    apps/transform.cpp transformp)
SINGLESOURCE_EXECUTABLE(GENERATE_XYZHANDLER,   multiresolution_xyz          apps/xyz.cpp       xyz)

if (CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(OUTPUTDIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
elseif (CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE)
  set(OUTPUTDIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE})
elseif (CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG)
  set(OUTPUTDIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG})
else()
  set(OUTPUTDIR ${CMAKE_CURRENT_BINARY_DIR})
endif()

if(${MINGW})
  if (NOT MINGW_DLLS_COPIED)
    get_filename_component( MINGWPATH ${CMAKE_CXX_COMPILER} PATH )
    file(GLOB DLL_FILES ${MINGWPATH}/*.dll)
    file(COPY ${DLL_FILES} DESTINATION ${OUTPUTDIR})
    set(MINGW_DLLS_COPIED "True")
  endif()
endif()

if (SET_CONFIGURE_FILE)
  if (NOT CONFIGURE_OUTPUT_PATH)
    set(CONFIGURE_OUTPUT_PATH ${OUTPUTDIR})
  endif()
  if (NOT FILESEP)
    if (WIN32)
      set(FILESEP \\)
    else()
      set(FILESEP /)
    endif()
  endif()
  if (NOT SLICER_PATH)
    string(REPLACE / ${FILESEP} SLICER_PATH "./")
  endif()
  if (NOT SLICER_EXEC)
    string(REPLACE / ${FILESEP} SLICER_EXEC "./slicer")
    if (WIN32)
      set(SLICER_EXEC ${SLICER_EXEC}.exe)
    endif()
  endif()
  if (NOT SHOWCONTOURS_SCRIPTPATH)
    string(REPLACE / ${FILESEP} SHOWCONTOURS_SCRIPTPATH "./pyclipper")
  endif()
  if (NOT SLICEVIEWER_SCRIPTPATH)
    string(REPLACE / ${FILESEP} SLICEVIEWER_SCRIPTPATH "./pyclipper")
  endif()
  if (NOT INITIAL_MESH_FILE)
    message(FATAL_ERROR "if SET_CONFIGURE_FILE is ON, you must supply to CMake a path to a STL file in the variable INITIAL_MESH_FILE")
  endif()
  string(REPLACE / ${FILESEP} INITIAL_MESH_FILE ${INITIAL_MESH_FILE})
  if (USE_PYTHON)
    if (NOT PYPATH)
      message(FATAL_ERROR "if SET_CONFIGURE_FILE is ON, you must supply to CMake the abolute path to the python executable in the variable PYPATH")
    endif()
    if (NOT SHOW_RESULT_PARAMETERS)
      set(SHOW_RESULT_PARAMETERS --show\ 2d)
    endif()
  else()
    if (NOT PYPATH)
      set(PYPATH "python")
    endif()
    if (NOT SHOW_RESULT_PARAMETERS)
      set(SHOW_RESULT_PARAMETERS )
    endif()
  endif()
  configure_file(${SRC}/config.template.txt ${CONFIGURE_OUTPUT_PATH}/config.txt)
  configure_file(${SRC}/params.template.txt ${CONFIGURE_OUTPUT_PATH}/params.txt)
endif()
