CMAKE_MINIMUM_REQUIRED(VERSION 2.8.9)

option(ENABLE_SLICER_LOGGING "enable slicer debug output to file" ON) #this option has to be coordinated with the one used in the slicer!!!!

option(USE_PYTHON_DEBUG      "add support for viewing contours in python for debugging" ON)
option(USE_PYTHON_VIEWER     "add support for viewing slices in python" ON)
option(GENERATE_LIBRARY      "generate shared library"  ON)
option(GENERATE_EXECUTABLE   "generate standalone executable"  ON)
option(GENERATE_CS_INTERFACE "generate the C# client of the DLL shared library (only in Visual Studio)" ${GENERATE_LIBRARY})
option(GENERATE_CS_AUTOCAD   "generate a .NET AutoCAD plugin that uses the C# client (only in Visual Studio, requires the AutoCAD SDK to be installed, and the paths to AutoCAD to be defined)" ${GENERATE_CS_INTERFACE})
option(GENERATE_SVGCONVERTER "generate svg converter executable"  ON)
option(GENERATE_DXFCONVERTER "generate dxf converter executable"  ON)
option(GENERATE_NANOCONVERTER "generate nanoscribe converter executable"  ON)
option(GENERATE_XYZHANDLER   "generate utility to process xyz point cloud files"  ON)
option(GENERATE_TRANSFORMER  "generate executable to apply transformations to paths"  ON)
option(GENERATE_FILEFILTER   "generate filter for paths files"  ON)
option(GENERATE_FILESPLITTER "generate a grid splitter for paths files"  ON)
option(GENERATE_FILEINFO     "generate info dumper for paths files"  ON)
option(GENERATE_FILEUNION    "generate tool to merge several paths files into one"  ON)
option(GENERATE_FILETOUCH    "generate slice header setter for paths files"  ON)

option(CLIPPER_USE_ARENA       "enable ArenaMemoryManager for ClipperLib" ON)
set(INITIAL_ARENA_SIZE "52428800" CACHE STRING "Initial size for ArenaMemoryManager")

option(COMPILE_OPTIMIZATIONS "optimizations that may make the build process significantly slower" ON)
if(GENERATE_LIBRARY OR GENERATE_EXECUTABLE)
  set(GENERATE_LIBRARY_OR_EXECUTABLE ON)
else()
  set(GENERATE_LIBRARY_OR_EXECUTABLE OFF)
endif()
option(SET_CONFIGURE_FILE    "generate example configuration files (the variables PYTHON_EXECUTABLE and INITIAL_MESH_FILE must be set!!!)" ${GENERATE_LIBRARY_OR_EXECUTABLE})

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}")

if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if (NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if (NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if (NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}")
endif()
set(OUTPUTDIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

set(PYTHON_EXECUTABLE "" CACHE PATH "vanilla python executable")
set(PYTHON_EXECUTABLE_FOR_SETUP "" CACHE PATH "python executable for use with setup.py (if using WinPython, must be \"WinPython Interpreter.exe\"")

if( ("${PYTHON_EXECUTABLE}" STREQUAL "") OR ("${PYTHON_EXECUTABLE_SETUP}" STREQUAL "") )
  find_program(PYTHON_EXECUTABLE_FOUND python)
  if(PYTHON_EXECUTABLE_FOUND)
    if("${PYTHON_EXECUTABLE}" STREQUAL "")
      set(PYTHON_EXECUTABLE "${PYTHON_EXECUTABLE_FOUND}")
    endif()
    if("${PYTHON_EXECUTABLE_SETUP}" STREQUAL "")
      set(PYTHON_EXECUTABLE_SETUP "${PYTHON_EXECUTABLE_FOUND}")
    endif()
  endif()
endif()

set(SHOW_RESULT_PARAMETERS "--show 2d" CACHE PATH "configuration option for the example param.txt file")
set(INITIAL_MESH_FILE "PUT_HERE_YOUR_STL_FILE" CACHE PATH "configuration option for the example param.txt file")

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

set(CLIPPER_BASE_DIR "" CACHE PATH "tweaked clipper/iopaths base dir")
if("${CLIPPER_BASE_DIR}" STREQUAL "")
  if (EXISTS ${SRC}/../clipper)
    set(CLIPPER_BASE_DIR ${SRC}/../clipper)
  else()
    message(FATAL_ERROR "Please set the variable CLIPPER_BASE_DIR to the base path to the clipper/iopaths directory")
  endif()
endif()

#as far as I understand, this will enable users of cmake-gui to set this directory
set(Boost_INCLUDE_DIR "" CACHE PATH "path to boost library")
if("${Boost_INCLUDE_DIR}" STREQUAL "")
  #try to find boost! first, let's see if we have it in the parent directory
  if(EXISTS ${SRC}/../boost/boost)
    set(Boost_INCLUDE_DIR ${SRC}/../boost)
  else()
    #try to find it with find_package
    find_package(Boost 1.55.0 QUIET COMPONENTS program_options)
    if (NOT Boost_FOUND)
      message(FATAL_ERROR "Error: please set the variable Boost_INCLUDE_DIR to the path to the Boost include directory (Boost 1.55.0 or higher required).")
    endif()
    set(BOOST_FINDPACKAGE_EXECUTED ON)
  endif()
endif()

if (CLIPPER_USE_ARENA)
  ADD_DEFINITIONS(-DCLIPPER_USE_ARENA -DINITIAL_ARENA_SIZE=${INITIAL_ARENA_SIZE})
endif()

set(clipper_include_dir ${CLIPPER_BASE_DIR}/clipper)
set(iopaths_include_dir ${CLIPPER_BASE_DIR}/iopaths)
set(clipper_source ${clipper_include_dir}/clipper.cpp)
set(clipper_header ${clipper_include_dir}/clipper.hpp)
set(alloc_header   ${clipper_include_dir}/allocation_schemes.hpp)
set(iopaths_source ${iopaths_include_dir}/iopaths.cpp)
set(iopaths_header ${iopaths_include_dir}/iopaths.hpp)
set(common_header  ${iopaths_include_dir}/common.hpp)

if(NOT EXISTS ${clipper_source} OR NOT EXISTS ${iopaths_source} OR NOT EXISTS ${clipper_header} OR NOT EXISTS ${iopaths_header} OR NOT EXISTS ${common_header} OR NOT EXISTS ${alloc_header})
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
endif()

if (ENABLE_SLICER_LOGGING)
    ADD_DEFINITIONS(-DSLICER_USE_DEBUG_FILE) #this is used by several subprojects
endif()

set (INCLUDE_DIRS
  ${clipper_include_dir}
  ${iopaths_include_dir}
  ${Boost_INCLUDE_DIR}
  ${SRC}/multi
  ${SRC}/interfaces
  ${SRC}/python
  ${SRC}/shared
  ${SRC}/apps
  )
include_directories(${INCLUDE_DIRS})

set(CORESOURCES
  ${alloc_header}
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
  multi/pathsplitter.hpp
  multi/pathsplitter.cpp
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
  interfaces/pathwriter_multifile.hpp
  interfaces/pathwriter_multifile.tpp
  interfaces/pathwriter_dxf.hpp
  interfaces/pathwriter_dxf.cpp
  interfaces/pathwriter_nanoscribe.hpp
  interfaces/pathwriter_nanoscribe.cpp
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

#as far as I understand, this will enable users of cmake-gui to set this directory
set(BOOST_PROGRAMOPTIONS_DIR "" CACHE PATH "path to boost::program_options source files")
if(NOT BOOST_FINDPACKAGE_EXECUTED)
  set(Boost_LIBRARIES "" CACHE PATH "If BOOST_PROGRAMOPTIONS_DIR is empty, this must include the absolute path to the static library program_options")
endif()

if("${BOOST_PROGRAMOPTIONS_DIR}" STREQUAL "")
  #try to find program_options's source code in a known locations
  if(EXISTS ${Boost_INCLUDE_DIR}/libs/program_options/src)
    set(BOOST_PROGRAMOPTIONS_DIR ${Boost_INCLUDE_DIR}/libs/program_options/src)
  elseif(EXISTS ${SRC}/../boost/libs/program_options/src)
    set(BOOST_PROGRAMOPTIONS_DIR ${SRC}/../boost/libs/program_options/src)
  elseif(BOOST_FINDPACKAGE_EXECUTED)
    if (NOT Boost_PROGRAM_OPTIONS_FOUND)
      message(FATAL_ERROR "Error: could not find neither the sources of boost::program_options nor the compiled binaries!")
    endif()
  else()
    if("${Boost_LIBRARIES}" STREQUAL "")
      message(FATAL_ERROR "Error: we require boost::program_options in any of these forms: either the sources in BOOST_PROGRAMOPTIONS_DIR or the compiled static library in Boost_LIBRARIES")
    endif()
  endif()
endif()

if(NOT "${BOOST_PROGRAMOPTIONS_DIR}" STREQUAL "")
  ADD_LIBRARY(miniboostlib STATIC
      ${BOOST_PROGRAMOPTIONS_DIR}/cmdline.cpp
      #${BOOST_PROGRAMOPTIONS_DIR}/config_file.cpp
      ${BOOST_PROGRAMOPTIONS_DIR}/options_description.cpp
      ${BOOST_PROGRAMOPTIONS_DIR}/parsers.cpp
      ${BOOST_PROGRAMOPTIONS_DIR}/variables_map.cpp
      ${BOOST_PROGRAMOPTIONS_DIR}/value_semantic.cpp
      ${BOOST_PROGRAMOPTIONS_DIR}/positional_options.cpp
      ${BOOST_PROGRAMOPTIONS_DIR}/utf8_codecvt_facet.cpp
      ${BOOST_PROGRAMOPTIONS_DIR}/convert.cpp
      #${BOOST_PROGRAMOPTIONS_DIR}/winmain.cpp
      #${BOOST_PROGRAMOPTIONS_DIR}/split.cpp
      )
  if ((NOT WIN32) AND CMAKE_COMPILER_IS_GNUCC)
    target_compile_options(miniboostlib PUBLIC "-fPIC" "-Wno-deprecated-declarations")
  endif()
  set(Boost_LIBRARIES miniboostlib)
endif()

ADD_LIBRARY(corelib STATIC ${CORESOURCES})
if(USE_PYTHON_DEBUG)
  set_property(TARGET corelib APPEND PROPERTY COMPILE_DEFINITIONS "CORELIB_USEPYTHON")
endif()
if ((NOT WIN32) AND CMAKE_COMPILER_IS_GNUCC)
  target_compile_options(corelib PUBLIC "-fPIC")
endif()
  
if(GENERATE_LIBRARY)
  set(LIBRARY_SOURCES
    shared/multiresolution.h
    shared/multiresolution.cpp
    )
  ADD_LIBRARY(multiresolution_library SHARED ${LIBRARY_SOURCES})
  set_target_properties(multiresolution_library PROPERTIES COMPILE_DEFINITIONS "LIBRARY_EXPORTS")
  set_target_properties(multiresolution_library PROPERTIES OUTPUT_NAME multires)
  target_link_libraries(multiresolution_library corelib ${Boost_LIBRARIES})
endif()

if (NOT FILESEP)
  if (WIN32)
    set(FILESEP \\)
  else()
    set(FILESEP /)
  endif()
endif()

if(MSVC)
  if(GENERATE_LIBRARY)
    if(GENERATE_CS_INTERFACE)
      set(CS_OUTPUT_DIR "${OUTPUTDIR}")
      set(MultiSlicerInterface_SOURCE_DIR_PREFIX "${SRC}\\CS\\MultiSlicerInterface\\")
      set(multiresolution_library_GUID_CMAKE "b3b551ad-5df5-4538-a482-0025ed6d4e2f" CACHE INTERNAL "")
      set(MULTIRES_LIBRARY_PROJECT_FILE "${CMAKE_CURRENT_BINARY_DIR}/multiresolution_library.vcxproj")
      set(MultiSlicerInterface_PROJECT_FILE "${CMAKE_CURRENT_BINARY_DIR}/MultiSlicerInterface.csproj")
      configure_file(${MultiSlicerInterface_SOURCE_DIR_PREFIX}MultiSlicerInterface.csproj.template ${MultiSlicerInterface_PROJECT_FILE})
      include_external_msproject(MultiSlicerInterface ${MultiSlicerInterface_PROJECT_FILE}
                                 GUID CB80C200-CA82-40A9-B7B7-F3B57578CEF2
                                 #TYPE
                                 PLATFORM AnyCPU)
      if(GENERATE_CS_AUTOCAD)
        set(AUTOCAD_PATH_PREFIX "C:\\Program Files\\Autodesk\\AutoCAD 2016" CACHE PATH "path to AutoCAD libraries (accoremgd.dll et al)")
        string(REPLACE / ${FILESEP} AUTOCAD_PATH_PREFIX "${AUTOCAD_PATH_PREFIX}")
        set(AUTOCAD_DLL_NAMES accoremgd AcCui acdbmgd acdbmgdbrep acmgd AcWindows)
        list(APPEND AUTOCAD_DLL_PATHS "")
        list(APPEND AUTOCAD_DLL_REFERENCES_LIST "")
        foreach(AUTOCAD_DLL_NAME ${AUTOCAD_DLL_NAMES})
          set(AUTOCAD_DLL_PATH "${AUTOCAD_PATH_PREFIX}\\${AUTOCAD_DLL_NAME}.dll")
          if (NOT EXISTS "${AUTOCAD_DLL_PATH}")
            message(FATAL_ERROR "Error: Building the AutoCAD plugin requires the AutoCAD DLLs, but this DLL could not be found: ${AUTOCAD_DLL_PATH}\n    You have two options:\n        (a) set GENERATE_CS_INTERFACE to OFF,\n        (b) make sure that AutoCAD SDK is installed and AUTOCAD_PATH_PREFIX actually points to the directory where the AutoCAD DLLs are located (currently it is ${AUTOCAD_PATH_PREFIX} )")
          endif()
          list(APPEND AUTOCAD_DLL_PATHS "${AUTOCAD_DLL_PATH}")
          list(APPEND AUTOCAD_DLL_REFERENCES_LIST "    <Reference Include=\"${AUTOCAD_DLL_NAME}\">\n        <HintPath>${AUTOCAD_DLL_PATH}</HintPath>\n    </Reference>")
        endforeach()
        string(REPLACE ";" "\n" AUTOCAD_DLL_REFERENCES "${AUTOCAD_DLL_REFERENCES_LIST}")
        
        MESSAGE("Please set AUTOCAD_PATH_PREFIX if necessary. Currently, it is ${AUTOCAD_PATH_PREFIX}")
        set(AutoCADMulti_SOURCE_DIR_PREFIX "${SRC}\\CS\\AutoCADMulti\\")
        set(AutoCADMulti_PROJECT_FILE "${CMAKE_CURRENT_BINARY_DIR}/AutoCADMulti.csproj")
        configure_file(${AutoCADMulti_SOURCE_DIR_PREFIX}AutoCADMulti.csproj.template ${AutoCADMulti_PROJECT_FILE})
        include_external_msproject(AutoCADMulti ${AutoCADMulti_PROJECT_FILE}
                                   GUID 556AA3FF-02C1-4D79-ADD1-5B4B6BFCD62D
                                   #TYPE
                                   PLATFORM AnyCPU)
      endif()
    endif()
  endif()
endif()
  
if(GENERATE_EXECUTABLE)
  set(EXECSOURCES apps/standalone.cpp)
  if(USE_PYTHON_VIEWER)
    set(EXECSOURCES
      ${EXECSOURCES}
      python/sliceviewer.hpp
      python/sliceviewer.cpp
      )
  endif()
  ADD_EXECUTABLE(multiresolution_executable ${EXECSOURCES})
  set_target_properties(multiresolution_executable PROPERTIES OUTPUT_NAME multires)
  if (USE_PYTHON)
    set_property(TARGET multiresolution_executable APPEND PROPERTY COMPILE_DEFINITIONS "STANDALONE_USEPYTHON")
  endif()
  target_link_libraries(multiresolution_executable corelib ${Boost_LIBRARIES})
endif()

function(SINGLESOURCE_EXECUTABLE GENERATE_OPTION TARGETNAME SOURCEFILE EXENAME)
  if(${GENERATE_OPTION})
    set(SINGLESOURCE ${SOURCEFILE})
    ADD_EXECUTABLE(${TARGETNAME} ${SINGLESOURCE})
    set_target_properties(${TARGETNAME} PROPERTIES OUTPUT_NAME ${EXENAME})
    target_link_libraries(${TARGETNAME} corelib ${Boost_LIBRARIES})
  endif()
endfunction(SINGLESOURCE_EXECUTABLE)

SINGLESOURCE_EXECUTABLE(GENERATE_SVGCONVERTER  multiresolution_svgconverter apps/svg.cpp        svgconv)
SINGLESOURCE_EXECUTABLE(GENERATE_DXFCONVERTER  multiresolution_dxfconverter apps/dxf.cpp        dxfconv)
SINGLESOURCE_EXECUTABLE(GENERATE_NANOCONVERTER multiresolution_nanoscribe   apps/nanoscribe.cpp nanoconv)
SINGLESOURCE_EXECUTABLE(GENERATE_FILEFILTER    multiresolution_filefilter   apps/filter.cpp     filterp)
SINGLESOURCE_EXECUTABLE(GENERATE_FILESPLITTER  multiresolution_filesplitter apps/splitter.cpp   splitterp)
SINGLESOURCE_EXECUTABLE(GENERATE_FILEINFO      multiresolution_fileinfo     apps/info.cpp       infop)
SINGLESOURCE_EXECUTABLE(GENERATE_FILEUNION     multiresolution_fileunion    apps/union.cpp      unionp)
SINGLESOURCE_EXECUTABLE(GENERATE_FILETOUCH     multiresolution_filetouch    apps/touch.cpp      touchp)
SINGLESOURCE_EXECUTABLE(GENERATE_TRANSFORMER   multiresolution_transform    apps/transform.cpp  transformp)
SINGLESOURCE_EXECUTABLE(GENERATE_XYZHANDLER    multiresolution_xyz          apps/xyz.cpp        xyz)

if(${MINGW})
  if (NOT MINGW_DLLS_COPIED)
    get_filename_component( MINGWPATH ${CMAKE_CXX_COMPILER} PATH )
    file(GLOB DLL_FILES ${MINGWPATH}/*.dll)
    file(COPY ${DLL_FILES} DESTINATION ${OUTPUTDIR})
    set(MINGW_DLLS_COPIED "True")
  endif()
endif()

set(SLICER_PATH "./" CACHE PATH "Directory of the slicer executable")
if (WIN32)
  set(SLICER_EXEC "./slicer.exe" CACHE PATH "Path to the slicer executable")
else()
  set(SLICER_EXEC "./slicer" CACHE PATH "Path to the slicer executable")
endif()
set(SHOWCONTOURS_SCRIPTPATH "./pyclipper" CACHE PATH "Directory of the Python script for showContours()")
set(SLICEVIEWER_SCRIPTPATH  "./pyclipper" CACHE PATH "Directory of the Python script for the slice viewer")

if (SET_CONFIGURE_FILE)
  if (NOT CONFIGURE_OUTPUT_PATH)
    set(CONFIGURE_OUTPUT_PATH ${OUTPUTDIR})
  endif()
  if (USE_PYTHON)
    if ("${PYTHON_EXECUTABLE}" STREQUAL "")
      message(FATAL_ERROR "if SET_CONFIGURE_FILE is ON, you must supply to CMake the abolute path to the python executable in the variable PYTHON_EXECUTABLE")
    endif()
  endif()
  string(REPLACE / ${FILESEP} SLICER_PATH             "${SLICER_PATH}")
  string(REPLACE / ${FILESEP} SLICER_EXEC             "${SLICER_EXEC}")
  string(REPLACE / ${FILESEP} SHOWCONTOURS_SCRIPTPATH "${SHOWCONTOURS_SCRIPTPATH}")
  string(REPLACE / ${FILESEP} SLICEVIEWER_SCRIPTPATH  "${SLICEVIEWER_SCRIPTPATH}")
  string(REPLACE / ${FILESEP} INITIAL_MESH_FILE       "${INITIAL_MESH_FILE}")
  string(REPLACE / ${FILESEP} PYTHON_EXECUTABLE       "${PYTHON_EXECUTABLE}")
  configure_file(${SRC}/config.template.txt ${CONFIGURE_OUTPUT_PATH}/config.txt)
  configure_file(${SRC}/params.template.txt ${CONFIGURE_OUTPUT_PATH}/params.txt)
endif()
