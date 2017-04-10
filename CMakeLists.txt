CMAKE_MINIMUM_REQUIRED(VERSION 3.0) #maybe it actually requires higher, but at most 3.5...

include(config.cmake)

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}")

#multiconfig generators place the files in directories matching the configuration. We want to override that behaviour, so the output directory is the one set in CMAKE_CURRENT_BINARY_DIR
MACRO(SETBINARYDIR DIRVAR)
   if(NOT ${DIRVAR})
     set(${DIRVAR} "${CMAKE_CURRENT_BINARY_DIR}")
   endif()
ENDMACRO()
SETBINARYDIR(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
SETBINARYDIR(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE)
SETBINARYDIR(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG)
SETBINARYDIR(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO)
SETBINARYDIR(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL)
SETBINARYDIR(CMAKE_LIBRARY_OUTPUT_DIRECTORY)
SETBINARYDIR(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE)
SETBINARYDIR(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG)
SETBINARYDIR(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO)
SETBINARYDIR(CMAKE_LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL)
set(OUTPUTDIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

if( ("${PYTHON_EXECUTABLE}" STREQUAL "") )
  find_program(PYTHON_EXECUTABLE_FOUND python)
  if(PYTHON_EXECUTABLE_FOUND)
    set(PYTHON_EXECUTABLE "${PYTHON_EXECUTABLE_FOUND}")
  endif()
endif()

if (USE_PYTHON_VIEWER AND NOT ("${PYTHON_EXECUTABLE}" STREQUAL ""))
  set(USE_PYTHON ON)
else()
  if (NOT ("${PYTHON_EXECUTABLE}" STREQUAL ""))
    message(WARNING "Option USE_PYTHON_VIEWER was set, but could not find the path to the python executable. Please set it in the PYTHON_EXECUTABLE variable")
  endif()
  set(USE_PYTHON OFF)
endif()

if(NOT MAKEMR_LIBRARY AND NOT MAKEMR_EXECUTABLE)
  message(FATAL_ERROR "PLEASE SET ON AT LEAST ONE OF THESE FLAGS: MAKEMR_LIBRARY, MAKEMR_EXECUTABLE")
endif()

if (USE_PYTHON OR MAKEMR_EXECUTABLE)
  if (USE_PYTHON AND NOT MAKEMR_EXECUTABLE)
    set(MSG "FOR THE PYTHON CALLER CODE!!!")
  endif()
  if (NOT USE_PYTHON AND MAKEMR_EXECUTABLE)
    set(MSG "FOR THE EXTERNAL SLICER MANAGER!!!")
  endif()
  if (USE_PYTHON AND MAKEMR_EXECUTABLE)
    set(MSG "FOR THE EXTERNAL SLICER MANAGER AND THE PYTHON CALLER CODE!!!")
  endif()
  message("PLEASE MAKE SURE TO USE A CONFIGURATION FILE WITH CORRECT DEFINITIONS ${MSG}")
endif()

if("${CLIPPER_BASE_DIR}" STREQUAL "")
  if (EXISTS ${SRC}/../clipper)
    set(CLIPPER_BASE_DIR ${SRC}/../clipper)
  else()
    message(FATAL_ERROR "Please set the variable CLIPPER_BASE_DIR to the base path to the clipper/iopaths directory")
  endif()
endif()

PROJECT(multiresolution)

if(NOT(CMAKE_SIZEOF_VOID_P EQUAL 8))
  message(FATAL_ERROR "This code is designed only for x64 systems! Size of void pointer: " ${CMAKE_SIZEOF_VOID_P})
endif()

set(DO_COMPILE_PROGRAMOPTIONS OFF)
if(COMPILE_PROGRAMOPTIONS AND EXISTS ${BOOST_ROOT_PATH}/boost/program_options AND EXISTS ${BOOST_ROOT_PATH}/libs/program_options/src)
  set(Boost_INCLUDE_DIRS ${BOOST_ROOT_PATH})
  set(BOOST_PROGRAMOPTIONS_DIR ${BOOST_ROOT_PATH}/libs/program_options/src)
  set(DO_COMPILE_PROGRAMOPTIONS ON)
  #BOOST_ALL_NO_LIB: disable autolink feature in BOOST causing trouble because we are brewing outside of BOOST infrastructure just what we need
  ADD_DEFINITIONS(-DBOOST_ALL_NO_LIB)
endif()
if(NOT DO_COMPILE_PROGRAMOPTIONS)
  set(BOOST_ROOT "${BOOST_ROOT_PATH}")      
  find_package(Boost 1.59.0 QUIET COMPONENTS program_options)
  if (NOT Boost_FOUND)
    message(FATAL_ERROR "Could not find BOOST. You can set BOOST_ROOT_PATH to the base directory of BOOST to provide a hint of its location")
  endif()
endif()

if (CLIPPER_USE_ARENA)
  ADD_DEFINITIONS(-DCLIPPER_USE_ARENA -DINITIAL_ARENA_SIZE=${INITIAL_ARENA_SIZE} -DBIGCHUNK_ARENA_SIZE=${BIGCHUNK_ARENA_SIZE})
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

SET(CMAKE_BUILD_TYPE "Release" CACHE STRING "Release type")

STRING(TOLOWER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_lowercase)
IF (COMPILE_OPTIMIZATIONS AND NOT "${CMAKE_BUILD_TYPE_lowercase}" STREQUAL "debug")
  SET(USE_COMPILE_OPTIMIZATIONS ON)
ELSE()
  SET(USE_COMPILE_OPTIMIZATIONS OFF)
ENDIF()

ADD_DEFINITIONS(-DCLIPPER_USE_INTRINSIC_128)
if (MSVC)
  #_CRT_SECURE_NO_WARNINGS: disable annoying warnings
  ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)
  if(USE_COMPILE_OPTIMIZATIONS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:fast /Ox /GF /Gy /GS- ")
  endif()
else()
  ADD_DEFINITIONS(-std=c++11)
  if(USE_COMPILE_OPTIMIZATIONS)
    #WARNING: DO NOT USE -march=native if you are going to distribute the binaries!!!!!!
    ADD_DEFINITIONS(-Ofast -ffast-math -march=native)
  endif()
endif()

#avoid exporting all symbols in shared libraries
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

if (ENABLE_SLICER_LOGGING)
    ADD_DEFINITIONS(-DSLICER_USE_DEBUG_FILE) #this is used by several subprojects
endif()

set (INCLUDE_DIRS
  ${clipper_include_dir}
  ${iopaths_include_dir}
  ${Boost_INCLUDE_DIRS}
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
  multi/serialization.hpp
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

if (USE_PYTHON)
  set(CORESOURCES
    ${CORESOURCES}
    python/showcontours.hpp
    python/showcontours.cpp
    )
endif()

if(DO_COMPILE_PROGRAMOPTIONS)
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
  if (NOT MSVC)
    target_compile_options(miniboostlib PUBLIC "-Wno-deprecated-declarations")
    if (NOT WIN32)
      target_compile_options(miniboostlib PUBLIC "-fPIC")
    endif()
  endif()
  set(Boost_LIBRARIES miniboostlib)
endif()

ADD_LIBRARY(corelib STATIC ${CORESOURCES})
if(USE_PYTHON)
  set_property(TARGET corelib APPEND PROPERTY COMPILE_DEFINITIONS "CORELIB_USEPYTHON")
endif()
if (NOT WIN32)
  target_compile_options(corelib PUBLIC "-fPIC")
endif()
  
if(MAKEMR_LIBRARY)
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
  if(MAKEMR_LIBRARY)
    if(MAKEMR_CS_INTERFACE)
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
      if(MAKEMR_CS_AUTOCAD)
        string(REPLACE / ${FILESEP} AUTOCAD_PATH_PREFIX "${AUTOCAD_PATH_PREFIX}")
        set(AUTOCAD_DLL_NAMES accoremgd AcCui acdbmgd acdbmgdbrep acmgd AcWindows)
        list(APPEND AUTOCAD_DLL_PATHS "")
        list(APPEND AUTOCAD_DLL_REFERENCES_LIST "")
        foreach(AUTOCAD_DLL_NAME ${AUTOCAD_DLL_NAMES})
          set(AUTOCAD_DLL_PATH "${AUTOCAD_PATH_PREFIX}\\${AUTOCAD_DLL_NAME}.dll")
          if (NOT EXISTS "${AUTOCAD_DLL_PATH}")
            message(FATAL_ERROR "Error: Building the AutoCAD plugin requires the AutoCAD DLLs, but this DLL could not be found: ${AUTOCAD_DLL_PATH}\n    You have two options:\n        (a) set MAKEMR_CS_INTERFACE to OFF,\n        (b) make sure that AutoCAD SDK is installed and AUTOCAD_PATH_PREFIX actually points to the directory where the AutoCAD DLLs are located (currently it is ${AUTOCAD_PATH_PREFIX} )")
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

set(EXECUTABLE_HEADERS apps/apputil.hpp)
  
if(MAKEMR_EXECUTABLE)
  set(EXECSOURCES apps/standalone.cpp)
  if(USE_PYTHON)
    set(EXECSOURCES
      ${EXECSOURCES}
      python/sliceviewer.hpp
      python/sliceviewer.cpp
      )
  endif()
  ADD_EXECUTABLE(multiresolution_executable ${EXECSOURCES} ${EXECUTABLE_HEADERS})
  set_target_properties(multiresolution_executable PROPERTIES OUTPUT_NAME multires)
  if (USE_PYTHON)
    set_property(TARGET multiresolution_executable APPEND PROPERTY COMPILE_DEFINITIONS "STANDALONE_USEPYTHON")
  endif()
  target_link_libraries(multiresolution_executable corelib ${Boost_LIBRARIES})
endif()

function(SINGLESOURCE_EXECUTABLE MAKEMR_OPTION TARGETNAME SOURCEFILE EXENAME)
  if(${MAKEMR_OPTION})
    set(SINGLESOURCE ${SOURCEFILE})
    ADD_EXECUTABLE(${TARGETNAME} ${SINGLESOURCE} ${EXECUTABLE_HEADERS})
    set_target_properties(${TARGETNAME} PROPERTIES OUTPUT_NAME ${EXENAME})
    target_link_libraries(${TARGETNAME} corelib ${Boost_LIBRARIES})
  endif()
endfunction(SINGLESOURCE_EXECUTABLE)

SINGLESOURCE_EXECUTABLE(MAKEMR_SVGCONVERTER  multiresolution_svgconverter apps/svg.cpp        svgconv)
SINGLESOURCE_EXECUTABLE(MAKEMR_DXFCONVERTER  multiresolution_dxfconverter apps/dxf.cpp        dxfconv)
SINGLESOURCE_EXECUTABLE(MAKEMR_NANOCONVERTER multiresolution_nanoscribe   apps/nanoscribe.cpp nanoconv)
SINGLESOURCE_EXECUTABLE(MAKEMR_FILEFILTER    multiresolution_filefilter   apps/filter.cpp     filterp)
SINGLESOURCE_EXECUTABLE(MAKEMR_FILEFILTERZ   multiresolution_filefilterz  apps/filterz.cpp    filterz)
SINGLESOURCE_EXECUTABLE(MAKEMR_FILESPLITTER  multiresolution_filesplitter apps/splitter.cpp   splitterp)
SINGLESOURCE_EXECUTABLE(MAKEMR_FILEINFO      multiresolution_fileinfo     apps/info.cpp       infop)
SINGLESOURCE_EXECUTABLE(MAKEMR_FILEUNION     multiresolution_fileunion    apps/union.cpp      unionp)
SINGLESOURCE_EXECUTABLE(MAKEMR_FILETOUCH     multiresolution_filetouch    apps/touch.cpp      touchp)
SINGLESOURCE_EXECUTABLE(MAKEMR_TRANSFORMER   multiresolution_transform    apps/transform.cpp  transformp)
SINGLESOURCE_EXECUTABLE(MAKEMR_XYZHANDLER    multiresolution_xyz          apps/xyz.cpp        xyz)

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
set(PYTHONSCRIPTS_PATH "./pyclipper" CACHE PATH "Directory of the python scripts")

if(GENERATE_TESTS)
  #these variables should be in config.cmake, but they cannot be set to adequate default values from an file that can be included at different levels, so they have to be defined twice: once here and once in the parent's CmakeLists.txt
  set(DATATEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data_test" CACHE PATH "test STL files are fetched from this directory. It has to be an absolute path")
  set(TEST_DIR     "${OUTPUTDIR}/../test"      CACHE PATH "directory where test artifacts are output. It has to be an absolute path")
  set(TESTPREV_DIR "${OUTPUTDIR}/../testprev"  CACHE PATH "directory where test artifacts from a previous test run are preserved, to be compared with the ones created in TEST_DIR. It has to be an absolute path")
  include(testing.cmake)
endif()

if (GENERATE_CONFIGURE_FILE AND (MAKEMR_LIBRARY OR MAKEMR_EXECUTABLE))
  if (NOT CONFIGURE_OUTPUT_PATH)
    set(CONFIGURE_OUTPUT_PATH ${OUTPUTDIR})
  endif()
  if (USE_PYTHON)
    if ("${PYTHON_EXECUTABLE}" STREQUAL "")
      message(FATAL_ERROR "if GENERATE_CONFIGURE_FILE is on and USE_PYTHON are ON, you must supply to CMake the abolute path to the python executable in the variable PYTHON_EXECUTABLE")
    endif()
  endif()
  string(REPLACE / ${FILESEP} SLICER_PATH             "${SLICER_PATH}")
  string(REPLACE / ${FILESEP} SLICER_EXEC             "${SLICER_EXEC}")
  string(REPLACE / ${FILESEP} PYTHONSCRIPTS_PATH      "${PYTHONSCRIPTS_PATH}")
  string(REPLACE / ${FILESEP} INITIAL_MESH_FILE       "${INITIAL_MESH_FILE}")
  string(REPLACE / ${FILESEP} PYTHON_EXECUTABLE       "${PYTHON_EXECUTABLE}")
  configure_file(${SRC}/config.template.txt ${CONFIGURE_OUTPUT_PATH}/config.txt)
  configure_file(${SRC}/params.template.txt ${CONFIGURE_OUTPUT_PATH}/params.txt)
endif()
