#This CMakeLists.txt is not intended as a standalone project, but is to be used from ../CMakeLists.txt

CMAKE_MINIMUM_REQUIRED(VERSION 3.0) #maybe it actually requires higher, but at most 3.5...

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}")

PROJECT(multiresolution_cs LANGUAGES) #no compiled language, skip usual compiler  tests

if (NOT FILESEP)
  if (WIN32)
    set(FILESEP \\)
  else()
    set(FILESEP /)
  endif()
endif()

set(CS_OUTPUT_DIR "${OUTPUTDIR}")
set(MultiSlicerInterface_SOURCE_DIR_PREFIX "${SRC}\\MultiSlicerInterface\\")
set(MultiSlicerInterface_PROJECT_FILE "${CMAKE_CURRENT_BINARY_DIR}/MultiSlicerInterface.csproj")
configure_file(${MultiSlicerInterface_SOURCE_DIR_PREFIX}MultiSlicerInterface.csproj.template ${MultiSlicerInterface_PROJECT_FILE})
include_external_msproject(MultiSlicerInterface ${MultiSlicerInterface_PROJECT_FILE}
                           GUID CB80C200-CA82-40A9-B7B7-F3B57578CEF2
                           #TYPE
                           PLATFORM AnyCPU)
if(MAKEMR_CS_AUTOCAD)
  string(REPLACE / ${FILESEP} AUTOCAD_PATH_PREFIX "${AUTOCAD_PATH_PREFIX}")
  MESSAGE(STATUS "Please set AUTOCAD_PATH_PREFIX if necessary. Currently, it is ${AUTOCAD_PATH_PREFIX}")
  
  list(APPEND AUTOCAD_DLL_PATHS "")
  list(APPEND AUTOCAD_DLL_REFERENCES_LIST "")
  MACRO(ADD_AUTOCAD_DLLS)
    foreach(AUTOCAD_DLL_NAME ${ARGN})
      set(AUTOCAD_DLL_PATH "${AUTOCAD_PATH_PREFIX}\\${AUTOCAD_DLL_NAME}.dll")
      if (NOT EXISTS "${AUTOCAD_DLL_PATH}")
        message(FATAL_ERROR "Error: Building the AutoCAD plugin requires the AutoCAD DLLs, but this DLL could not be found: ${AUTOCAD_DLL_PATH}\n    You have two options:\n        (a) set MAKEMR_CS_AUTOCAD to OFF,\n        (b) make sure that AutoCAD's ObjectARX is installed and AUTOCAD_PATH_PREFIX actually points to the directory where the AutoCAD DLLs are located (currently it is ${AUTOCAD_PATH_PREFIX} )")
      endif()
      list(APPEND AUTOCAD_DLL_PATHS "${AUTOCAD_DLL_PATH}")
      list(APPEND AUTOCAD_DLL_REFERENCES_LIST "    <Reference Include=\"${AUTOCAD_DLL_NAME}\">\n        <HintPath>${AUTOCAD_DLL_PATH}</HintPath>\n    </Reference>")
    endforeach()
    string(REPLACE ";" "\n" AUTOCAD_DLL_REFERENCES "${AUTOCAD_DLL_REFERENCES_LIST}")
  ENDMACRO()
  
  ADD_AUTOCAD_DLLS(accoremgd acdbmgd)
  set(AutoCADMulti_SOURCE_DIR_PREFIX "${SRC}\\AutoCADMulti\\")
  set(AutoCADMulti_PROJECT_FILE "${CMAKE_CURRENT_BINARY_DIR}/AutoCADMulti.csproj")
  configure_file(${AutoCADMulti_SOURCE_DIR_PREFIX}AutoCADMulti.csproj.template ${AutoCADMulti_PROJECT_FILE})
  include_external_msproject(AutoCADMulti ${AutoCADMulti_PROJECT_FILE}
                             GUID 556AA3FF-02C1-4D79-ADD1-5B4B6BFCD62D
                             #TYPE
                             PLATFORM AnyCPU)
  ADD_AUTOCAD_DLLS(AcCui acdbmgdbrep acmgd AcWindows) #this keeps the DLLs from the previous call to the macro
  set(AutoCADMultiGUI_SOURCE_DIR_PREFIX "${SRC}\\AutoCADMultiGUI\\")
  set(AutoCADMultiGUI_PROJECT_FILE "${CMAKE_CURRENT_BINARY_DIR}/AutoCADMultiGUI.csproj")
  configure_file(${AutoCADMultiGUI_SOURCE_DIR_PREFIX}AutoCADMultiGUI.csproj.template ${AutoCADMultiGUI_PROJECT_FILE})
  include_external_msproject(AutoCADMultiGUI ${AutoCADMultiGUI_PROJECT_FILE}
                             GUID 17452815-071B-4051-BF89-80BCFC3EF454
                             #TYPE
                             PLATFORM AnyCPU)
endif()
