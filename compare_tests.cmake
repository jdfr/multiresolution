#this is intended as a standalone script. However, it should not be used from the directory of
#the multiresolution project, because it uses git to change the working copy, so the cmake scripts
#used in the test process may be changed or even removed from the working copy!
#Instead, if you try to run it from its original location, it will look for a known file from
#the repository and, if present, it will copy the test scripts to ../testmaster, and prompt the
#user to run the script again from there. All default values for the input variables are set
#with that location in mind. The contents of ../testmaster are disposable, can be easily regenerated
#if necessary.

#This test framework relies on the structure of the tests contained in testing.cmake, which are labeled along two dimensions:
#    size:  mini/full     (mini==small/fast, full==big/slow)
#    stage: put/exec/comp (put==copy/generate test input file, exec==compute output file, comp==compare output file with previous output)
#
#So, this test framework is prepared to compare two tests: in the first, only put/exec tests are performed,
#in the second, all put/exec/comp tests are performed.

#This test framework can work in two modes:
#
#  * in the first mode (option NOEXECA is OFF), it compares two states (current state/branch/tag/commit;
#    anything that can be fed to git checkout), running the tests in both of them, and comparing the
#    results of both test suites, to see if the changes to the sources have changed the outputs.
#    The variables are named XXXA for the first state and XXXB for the second
#
#  * in the second mode (option NOEXECA is ON), it assumes that there is already a ../testprev directory 
#    containing test outputs, and generates/builds/executes tests and compares their outputs against the
#    contents in ../testprev. Please note that in this mode, the XXXA variables are not used. This mode is
#    inteneded to be useful to repeatedly test 

#After running the script again from ../testmaster, if there were no problems, the files TEST.REVA.txt and
#TEST.REVB.txt will be generated, contaning the logs of the tests for revisions REVA and REVB, respectively
#(when NOEXECA is ON, only the second file will be generated, of course). 

# PLEASE USE WITH CARE
#   -This script has *lots* of arguments. Most of them have sane defaults, but some will have to be filled out
#   -When invoked from the multiresolution directory, the script will require arguments to configure testing.cmake
#   -When invoked outside the multiresolution directory, the script will require arguments to drive the tests.
#    These will have to be provided:
#          * REVA and REVB represent the git objects (branches/tags/commits/CURRENT state) to execute the tests
#          * If the build system at revisions REVA and REVB are compatible, the builds can be run on the same
#            directories, so in that case BUILDA and BUILDB can be the same, otherwise, they should be different.
#            If BUILDA/BUILDB is empty, the build step will not be performed.
#          * BINA and BINB are the binary output directories containing the executables for REVA and REVB, respectively.
#            If GENA, GENB, BUILDA and BUILDB are empty, you can use BINA and BINB to compare two different instances
#            that you have previously built.
#          * If there is already a build directory with a build system compatible with one of the revisions,
#            you can just point the corresponding BUILDA and/or BUILDB to that directory. Otherwise, you will
#            need to set GENA and/or GENB, the commands used to generate the build system (usually, cmake with lots
#            of arguments).
#          * Watch out! If GENA/GENB is not empty, the corresponding directory BUILDA/BUILDB will be *wiped out*
#            to avoid problems during the generation phase.
#          * The code in the multiresolution project has to be in sync with the code in other projects; most
#            notably in clipper, pyclipper and the standalone slic3r. Normally, changes that break the interfaces
#            are rare and far between, but it may be necessary to compare the test among such changes. For that
#            purpose, REVCLIPPERA, REVCLIPPERB, REVPYCLIPPERA, REVPYCLIPPERB, REVSLIC3RA and REVSLIC3RB represent
#            the revisions on each one of the other projects that have to be used together with REVA or REVB,
#            respectively. Their default values are CURRENT in all cases.
#   -When invoked outside the multiresolutino directory, it is quite difficult to set the arguments just right
#    from the command line, because of issues with paths and double quotes.
#    IT IS STRONGLY ADVISED TO EDIT THE FILE TO SET THE ARGUMENTS, INSTEAD OF PASSING THEM WITH -D ARG=VALUE.
#    Anyways, the master copy of the file will remain immutable in the multiresolution directory...

#PLEASE NOTE: currently, this script only cares about the history of the multiresolution project, but it depends on
#             several other projects for its operation, such as clipper, standalone slic3r and pyclipper. If necessary,
#             support for changing the state of the other repositories will be added.

#By default, the script just changes the working copy and then rebuilds the project. However,
#if the REVA and/or REVB versions of the working copy have changes to the build system that do
#not play well with the cached build configuration, the builds may fail. To alleviate this,
#the user can supply a command in GENA to generate "ex-novo" the build system for REVA (ditto
#for GENB and REVB).

#Why cmake? I work on this indistinctly in windows and linux environments; maintaining a *.bat and
#a *.sh script to do the same is a PITA, and cmake provides a way to execute (somewhat) platform-independent
#scripts, even if they are horribly cumbersome...


###########################
### CODE STARTS HERE ######
###########################

#Ideally, we should be able to write the CMakeLists.txt file once withut OUTPUTDIR, and then invoke cmake -D OUTPUTDIR="...",
#but there is a catch: if the -D argument is enclosed in double quotes, so will be the value of the variable during the execution of the script,
#potentially ruining things because using too many double quotes. To remedy that problem, we just rewrite CMakeLists.txt every time we
#have to change OUTPUTDIR
MACRO(WRITECMAKELISTS OUTPUTDIR)
  file(WRITE "${MASTERDIR}/CMakeLists.txt"
"
CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
project(TESTMASTER NONE)
set(OUTPUTDIR \"${OUTPUTDIR}\")
include(config.cmake)
include(testing.cmake)
")
ENDMACRO()

#the contents of this file are available only when we execute in the multiresolution directory, so we put them in a different file,
#as CMakeLists will be regenerated every time we need to change OUTPUTDIR
MACRO(WRITECMAKECONFIG)
  file(WRITE "${MASTERDIR}/config.cmake"
"
CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
project(TESTMASTER NONE)
set(PYTHON_EXECUTABLE  \"${PYTHON_EXECUTABLE}\")
set(PYTHONSCRIPTS_PATH \"${PYTHONSCRIPTS_PATH}\")
set(DATATEST_DIR       \"${DATATEST_DIR}\")
set(TEST_DIR           \"${TEST_DIR}\")
set(TESTPREV_DIR       \"${TESTPREV_DIR}\")
")
ENDMACRO()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config.template.txt")

  #IN THIS CASE, WE CREATE A FAKE BUILD SYSTEM TO RUN THE TESTS INDEPENDENTLY FROM THE PROJECT'S BUILD(S) SYSTEM(S)

  #variable to set the master test location
  set(MASTERDIR          "${CMAKE_CURRENT_SOURCE_DIR}/../testmaster"
      CACHE STRING       "master test directory where test scripts are copied before testing")
  
  #arguments necessary for testing.cmake
  set(PYTHON_EXECUTABLE  ""
      CACHE STRING       "path to python executable")
  set(PYTHONSCRIPTS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../pyclipper"
      CACHE STRING       "path to the required python scripts (pyclipper project)")
  set(DATATEST_DIR       "${CMAKE_CURRENT_SOURCE_DIR}/../data_test"
      CACHE STRING       "path to STL test files")
  set(TEST_DIR           "${CMAKE_CURRENT_SOURCE_DIR}/../test"
      CACHE STRING       "directory were the tests will put the generated files")
  set(TESTPREV_DIR       "${CMAKE_CURRENT_SOURCE_DIR}/../testprev"
      CACHE STRING       "directory where the generated files from a previous test run are located, to be compared with the generated files from the current run")

  #copy/generate test files
  file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/compare_tests.cmake" DESTINATION "${MASTERDIR}")
  file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/testing.cmake"       DESTINATION "${MASTERDIR}")
  if (WIN32)
    file(WRITE "${MASTERDIR}/compare_tests.bat" "cmake %* -P compare_tests.cmake")
  else()
    file(WRITE "${MASTERDIR}/tmp/compare_tests.sh" "cmake \"$@\" -P compare_tests.cmake")
    file(COPY "${MASTERDIR}/tmp/compare_tests.sh" DESTINATION "${MASTERDIR}" FILE_PERMISSIONS
         OWNER_EXECUTE OWNER_READ OWNER_WRITE
         GROUP_EXECUTE GROUP_READ GROUP_WRITE
         WORLD_EXECUTE WORLD_READ)
    file(REMOVE "${MASTERDIR}/tmp")
  endif()
  
  WRITECMAKECONFIG()
  WRITECMAKELISTS("UNDEFINED")
  
  execute_process(COMMAND ${CMAKE_COMMAND} .
                  WORKING_DIRECTORY ${MASTERDIR}
                  RESULT_VARIABLE RES)
  if(NOT "${RES}" STREQUAL "0")
    message(FATAL_ERROR "COULD NOT GENERATE MASTER TEST SYSTEM")
  endif()
  
  message(STATUS "test master system generated. To execute it, you can now execute the copy of this script in ${MASTERDIR}")
  
else()

  #IN THIS CASE, WE USE A PREVIOUSLY CREATED FAKE BUILD SYSTEM TO RUN THE TESTS INDEPENDENTLY FROM THE PROJECT'S BUILD(S) SYSTEM(S)
  
  #variable to set the master test location. We set it differently this time because in this case,
  #it must point to the directory the file is located in
  set(MASTERDIR          "${CMAKE_CURRENT_SOURCE_DIR}"
      CACHE STRING       "master test directory where test scripts are copied before testing")

  #Variables to configure the execution and comparison of the tests, when we execute the script in MASTERDIR
  option(NOEXECA "By default, REVA is gennerated/built/tested. However, if this option is ON, PREVDIR will be assumed to already contain tests, and only tests from REVB will be executed (also comparing with contents in PREVDIR" OFF)
  set(SRCDIR    "${CMAKE_CURRENT_SOURCE_DIR}/.."          CACHE STRING "base source directory; all projects should be subdirectories")
  set(BUILDA    "${CMAKE_CURRENT_SOURCE_DIR}/../build"    CACHE STRING "build directory for REVA (build system must be already generated, unless GENA is nonempty). If this paramenter is empty, the build is supposed to be already done")
  set(BUILDB    "${CMAKE_CURRENT_SOURCE_DIR}/../build"    CACHE STRING "build directory for REVB (build system must be already generated, unless GENB is nonempty). If this paramenter is empty, the build is supposed to be already done")
  set(BINA      "${CMAKE_CURRENT_SOURCE_DIR}/../bin"      CACHE STRING "binary directory for REVA")
  set(BINB      "${CMAKE_CURRENT_SOURCE_DIR}/../bin"      CACHE STRING "binary directory for REVB.")
  set(GENA      ""                                        CACHE STRING "If nonempty, this command will be executed with the intention to generate the build system for REVA")
  set(GENB      ""                                        CACHE STRING "If nonempty, this command will be executed with the intention to generate the build system for REVA")
  set(TESTDIR   "${CMAKE_CURRENT_SOURCE_DIR}/../test"     CACHE STRING "test output directory")
  set(PREVDIR   "${CMAKE_CURRENT_SOURCE_DIR}/../testprev" CACHE STRING "directory for previous test output (to compare both test runs during REVB testing)")
  set(CONFIG    "Release"                                 CACHE STRING "build configuration (for MSVS)")
  set(SUBSET    "mini"                                    CACHE STRING "label regex to include tests (if empty, all tests will be done)")
  set(OUTPUTA   "TEST.REVA.txt"                           CACHE STRING "Log file for the tests executed on revision REVA.")
  set(OUTPUTB   "TEST.REVB.txt"                           CACHE STRING "Log file for the tests executed on revision REVB.")
  #In any of the following arguments: if it is 'CURRENT', then the current state of the working tree is used.
  set(REVA          "CURRENT"                             CACHE STRING "first  branch/tag/commit for multiresolution project.")
  set(REVB          ""                                    CACHE STRING "second branch/tag/commit for multiresolution project.")
  set(REVCLIPPERA   "CURRENT"                             CACHE STRING "first branch/tag/commit for clipper project.")
  set(REVCLIPPERB   "CURRENT"                             CACHE STRING "second branch/tag/commit for clipper project.")
  set(REVPYCLIPPERA "CURRENT"                             CACHE STRING "first branch/tag/commit for pyclipper project.")
  set(REVPYCLIPPERB "CURRENT"                             CACHE STRING "second branch/tag/commit for pyclipper project.")
  set(REVSLIC3RA    "CURRENT"                             CACHE STRING "first branch/tag/commit for Slic3r project.")
  set(REVSLIC3RB    "CURRENT"                             CACHE STRING "second branch/tag/commit for Slic3r project.")


  #problematic...
  #separate_arguments(GENA)
  #separate_arguments(GENB)
  
  MACRO(ERRNOTSET ARG)
    if ("${${ARG}}" STREQUAL "")
      message(FATAL_ERROR "Please set the ${ARG} argument!!!")
    endif()
  ENDMACRO()
  
  ERRNOTSET(REVB)
  ERRNOTSET(BINA)
  ERRNOTSET(BINB)
  
  if ("${REVB}" STREQUAL "")
    message(FATAL_ERROR "Please set the REVB argument!!!")
  endif()

  find_program(GIT_FOUND git)

  if (NOT GIT_FOUND)
    message(FATAL_ERROR "cannot find git!!!!")
  endif()

  if ("${SUBSET}" STREQUAL "")
    set(TESTARGS )
  else()
    set(TESTARGS -L ${SUBSET})
  endif()

  #not strictly necessary, any non-defined variable will evaluate to empty...
  set(STASH_EXECUTED_multiresolution OFF)
  set(STASH_EXECUTED_clipper         OFF)
  set(STASH_EXECUTED_pyclipper       OFF)
  set(STASH_EXECUTED_Slic3r          OFF)

  MACRO(STASH PROJECT)
    set(STASH_EXECUTED_${PROJECT} ON)
    execute_process(COMMAND git rev-parse refs/stash
                    WORKING_DIRECTORY ${SRCDIR}/${PROJECT}
                    OUTPUT_VARIABLE REVPARSE1_${PROJECT}
                    ERROR_VARIABLE  REVPARSE1_${PROJECT})
    execute_process(COMMAND git stash
                    WORKING_DIRECTORY ${SRCDIR}/${PROJECT})
    execute_process(COMMAND git rev-parse refs/stash
                    WORKING_DIRECTORY ${SRCDIR}/${PROJECT}
                    OUTPUT_VARIABLE REVPARSE2_${PROJECT}
                    ERROR_VARIABLE  REVPARSE2_${PROJECT})
  ENDMACRO()

  MACRO(UNSTASH PROJECT)
    if (STASH_EXECUTED_${PROJECT} AND NOT "${REVPARSE1_${PROJECT}}" STREQUAL "${REVPARSE2_${PROJECT}}")
      execute_process(COMMAND git stash pop
                      WORKING_DIRECTORY ${SRCDIR}/${PROJECT})
    endif()
  ENDMACRO()

  MACRO(CHECKOUT PROJECT REV)
    execute_process(COMMAND git checkout ${REV}
                    WORKING_DIRECTORY ${SRCDIR}/${PROJECT}
                    RESULT_VARIABLE RES)
  ENDMACRO()

  #git maintains a history of visited revisions (the reflog), so git checkout @{-N} will go back to the last N-th visited revision
  MACRO(CHECKOUTPREV PROJECT BACK)
    execute_process(COMMAND git checkout @{${BACK}}
                    WORKING_DIRECTORY ${SRCDIR}/${PROJECT})
  ENDMACRO()

  #not strictly necessary, any non-defined variable will evaluate to empty...
  set(NUM_RESTORE_multiresolution "")
  set(NUM_RESTORE_clipper         "")
  set(NUM_RESTORE_pyclipper       "")
  set(NUM_RESTORE_Slic3r          "")
  
  MACRO(RESTORE_GIT PROJECT)
    if(NOT "${NUM_RESTORE_${PROJECT}}" STREQUAL "")
      CHECKOUTPREV(${PROJECT} ${NUM_RESTORE_${PROJECT}})
      UNSTASH(${PROJECT})
      set(NUM_RESTORE_${PROJECT} "")
    endif()
  ENDMACRO()
  
  MACRO(RESTORE_GIT_ALL)
    RESTORE_GIT(multiresolution)
    RESTORE_GIT(clipper)
    RESTORE_GIT(pyclipper)
    RESTORE_GIT(Slic3r)
  ENDMACRO()
  
  MACRO(RESTORE_GIT_IF_NOT_CURRENT_ALL A_OR_B)
    if (NOT "${REV${A_OR_B}}" STREQUAL "CURRENT")
        RESTORE_GIT(multiresolution)
    endif()
    if (NOT "${REVCLIPPER${A_OR_B}}" STREQUAL "CURRENT")
        RESTORE_GIT(clipper)
    endif()
    if (NOT "${REVPYCLIPPER${A_OR_B}}" STREQUAL "CURRENT")
        RESTORE_GIT(pyclipper)
    endif()
    if (NOT "${REVSLIC3R${A_OR_B}}" STREQUAL "CURRENT")
        RESTORE_GIT(Slic3r)
    endif()
  ENDMACRO()
  
  #safe checkout process, the first time we do it
  MACRO(SAFECHECKOUTFIRST A_OR_B PROJECT REV_VAR)
    if (NOT "${${REV_VAR}${A_OR_B}}" STREQUAL "CURRENT")
      STASH(${PROJECT})
      CHECKOUT(${PROJECT} ${${REV_VAR}${A_OR_B}})
      if(NOT "${RES}" STREQUAL "0")
        UNSTASH(${PROJECT})
        RESTORE_GIT_ALL()
       message(FATAL_ERROR "COULD NOT CHECKOUT ${PROJECT} <${${REV_VAR}${A_OR_B}}>")
      endif()
      #we only have checkout one revision, so to get the original checkout we'll have to go back in time one step to get the original checkout
      set(NUM_RESTORE_${PROJECT} -1)
    endif()
  ENDMACRO()
  
  #safe checkout process, the second time we do it
  MACRO(SAFECHECKOUTSECOND PROJECT REV_VAR)
    if ("${${REV_VAR}A}" STREQUAL "CURRENT")
      if (NOT "${${REV_VAR}B}" STREQUAL "CURRENT")
        STASH(${PROJECT})
        CHECKOUT(${PROJECT} ${${REV_VAR}B})
        if(NOT "${RES}" STREQUAL "0")
            UNSTASH(${PROJECT})
            RESTORE_GIT_ALL()
            message(FATAL_ERROR "COULD NOT CHECKOUT ${PROJECT} <${${REV_VAR}B}>")
        endif()
        #we only have checkout one revision, so to get the original checkout we'll have to go back in time one step to get the original checkout
        set(NUM_RESTORE_${PROJECT} -1)
      endif()
    else()
      if ("${${REV_VAR}B}" STREQUAL "CURRENT")
        RESTORE_GIT(${PROJECT})
      else()
        CHECKOUT(${PROJECT} ${${REV_VAR}B})
        if(NOT "${RES}" STREQUAL "0")
          RESTORE_GIT_ALL()
          message(FATAL_ERROR "COULD NOT CHECKOUT ${PROJECT} <${${REV_VAR}B}>")
        endif()
        #we have checkout two revisions, so to get the original checkout we'll have to go back in time two steps to get the original checkout
        set(NUM_RESTORE_${PROJECT} -2)
      endif()
    endif()
  ENDMACRO()
  
  if (NOEXECA)
  
    #IN THIS CASE, WE ONLY TEST REVB AND COMPARE ITS RESULTS AGAINST THE OUTPUT IN TESTPREV_DIR

    #remove test outputs, if present
    file(REMOVE_RECURSE "${TESTDIR}")

    #git checkout REVB
    SAFECHECKOUTFIRST(B multiresolution REV)
    SAFECHECKOUTFIRST(B clipper         REVCLIPPER)
    SAFECHECKOUTFIRST(B pyclipper       REVPYCLIPPER)
    SAFECHECKOUTFIRST(B Slic3r          REVSLIC3R)

  else()

    #IN THIS CASE, WE TEST FIRST REVA WITHOUT COMPARING; THEN TEST REVB AND COMPARE ITS RESULTS AGAINST REVA

    if (("${REVA}" STREQUAL "CURRENT") AND ("${REVB}" STREQUAL "CURRENT"))
      #this is a sanity check, but it should be disabled if we want to compare revisions of the *other* projects: clipper, pyclipper, Slic3r
      message(FATAL_ERROR "REVA and REVB cannot be both CURRENT at the same time!!!!")
    endif()

    #git checkout REVA
    SAFECHECKOUTFIRST(A multiresolution REV)
    SAFECHECKOUTFIRST(A clipper         REVCLIPPER)
    SAFECHECKOUTFIRST(A pyclipper       REVPYCLIPPER)
    SAFECHECKOUTFIRST(A Slic3r          REVSLIC3R)
    
    #generate build system for REVA
    if (NOT "${GENA}" STREQUAL "")
      if ("${BUILDA}" STREQUAL "")
        RESTORE_GIT_ALL()
        message(FATAL_ERROR "If GENA is not empty, BUILDA must also be nonempty!!!!")
      endif()
      file(REMOVE_RECURSE "${BUILDA}")
      file(MAKE_DIRECTORY "${BUILDA}")
      execute_process(COMMAND ${GENA}
                      WORKING_DIRECTORY ${BUILDA}
                      RESULT_VARIABLE RES)
      if(NOT "${RES}" STREQUAL "0")
        RESTORE_GIT_IF_NOT_CURRENT_ALL(A)
        message(FATAL_ERROR "COULD NOT GENERATE BUILD SYSTEM FOR <${REVA}>")
      endif()
    endif()
    
    #build REVA
    if (NOT "${BUILDA}" STREQUAL "")
      execute_process(COMMAND ${CMAKE_COMMAND} --build . --config ${CONFIG}
                      WORKING_DIRECTORY ${BUILDA}
                      RESULT_VARIABLE RES)
      if(NOT "${RES}" STREQUAL "0")
        RESTORE_GIT_IF_NOT_CURRENT_ALL(A)
        message(FATAL_ERROR "COULD NOT BUILD <${REVA}>")
      endif()
    endif()

    #remove previous test output
    file(REMOVE_RECURSE "${TESTDIR}")

    #test REVA
    WRITECMAKELISTS("${BINA}") #we do not care to specify OUTPUTDIR the first time
    execute_process(COMMAND ${CMAKE_COMMAND} .
                    WORKING_DIRECTORY ${MASTERDIR})
    execute_process(COMMAND ctest ${TESTARGS} -LE comp -C Release --output-log "${OUTPUTA}"
                    WORKING_DIRECTORY ${MASTERDIR})

    #move test output from test to testprev
    file(REMOVE_RECURSE "${PREVDIR}")
    file(RENAME "${TESTDIR}" "${PREVDIR}")

    #checkout REVB
    SAFECHECKOUTSECOND(multiresolution REV)
    SAFECHECKOUTSECOND(clipper         REVCLIPPER)
    SAFECHECKOUTSECOND(pyclipper       REVPYCLIPPER)
    SAFECHECKOUTSECOND(Slic3r          REVSLIC3R)
    
  endif()

  #generate build system for REVB
  if (NOT "${GENB}" STREQUAL "")
    if ("${BUILDB}" STREQUAL "")
      RESTORE_GIT_ALL()
      message(FATAL_ERROR "If GENB is not empty, BUILDB must also be nonempty!!!!")
    endif()
    file(REMOVE_RECURSE "${BUILDB}")
    file(MAKE_DIRECTORY "${BUILDB}")
    execute_process(COMMAND ${GENB}
                    WORKING_DIRECTORY ${BUILDB}
                    RESULT_VARIABLE RES)
    if(NOT "${RES}" STREQUAL "0")
      RESTORE_GIT_IF_NOT_CURRENT_ALL(B)
      message(FATAL_ERROR "COULD NOT GENERATE BUILD SYSTEM FOR <${REVB}>")
    endif()
  endif()

  #build REVB
  if (NOT "${BUILDB}" STREQUAL "")
    execute_process(COMMAND ${CMAKE_COMMAND} --build . --config ${CONFIG}
                    WORKING_DIRECTORY ${BUILDB}
                    RESULT_VARIABLE RES)
    if(NOT "${RES}" STREQUAL "0")
      RESTORE_GIT_IF_NOT_CURRENT_ALL(B)
      message(FATAL_ERROR "COULD NOT BUILD <${REVB}>")
    endif()
  endif()

  #test REVB
  WRITECMAKELISTS("${BINB}") #we do not care to specify OUTPUTDIR the first time
  execute_process(COMMAND ${CMAKE_COMMAND} .
                  WORKING_DIRECTORY ${MASTERDIR})
  execute_process(COMMAND ctest ${TESTARGS} -C Release --output-log "${OUTPUTB}"
                  WORKING_DIRECTORY ${MASTERDIR})

  #restore original checkout
  RESTORE_GIT_IF_NOT_CURRENT_ALL(B)

endif()


