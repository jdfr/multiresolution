# This script adds testing to this project (and also uses project pyclipper, so it adds some testing for it)
#
# Testing works at two levels: some tests generate results, and other tests compare these results against
# results from a previous test run, which must be in a known location
#
# This script must be able to run even outside of the scope of the project's CMakeLists.txt,
# so references to the targets should not be used.

#INPUT VARIABLES FOR THIS SCRIPT
#  PYTHON_EXECUTABLE
#    The tests require python. If not set, the script will attempt to find python. If it does not find it, it will error out.
#  PYTHONSCRIPTS_PATH
#    Path to the required python scripts (pyclipper project)
#  OUTPUTDIR
#    directory of the built binaries
#  DATATEST_DIR
#    directory with STL test files
#  TEST_DIR
#    directory were the tests will put the generated files
#  TESTPREV_DIR
#    directory where the generated files from a previous test run are located, to be compared with the generated files from the current run
#  USE_GCOV, LCOVPATH, GENHTMLPATH
#    to use lcov to generate code coverage reports after doing test targets

enable_testing()

if( ("${PYTHON_EXECUTABLE}" STREQUAL "") )
  find_program(PYTHON_EXECUTABLE_FOUND python)
  if(PYTHON_EXECUTABLE_FOUND)
    set(PYTHON_EXECUTABLE "${PYTHON_EXECUTABLE_FOUND}")
  else()
    message(FATAL_ERROR "Using the tests requires python, but could not find the path to the python executable. Please set it in the PYTHON_EXECUTABLE variable")
  endif()
endif()

###########################################################
###### TEST MACROS
###########################################################

MACRO(PREPARE_COMMAND_NAME NM)
    if(WIN32)
        set(${NM} "${OUTPUTDIR}/${NM}.exe")
    else()
        set(${NM} "${OUTPUTDIR}/${NM}")
    endif()
ENDMACRO()

#when this file is included in the project's CMakeLists.txt, we can obtain the same result
#with ADD_TEST(multiresolution_executable) and ADD_TEST(${multires}), but the second one 
#also works when used standalone, which is the mode configured in compare_tests.cmake
PREPARE_COMMAND_NAME(multires)
PREPARE_COMMAND_NAME(filterp)

MACRO(TEST_TEMPLATE TESTNAME WORKDIR)
  ADD_TEST(NAME ${TESTNAME}
           COMMAND ${ARGN}
           WORKING_DIRECTORY "${WORKDIR}")
ENDMACRO()

MACRO(TEST_TEMPLATE_LABEL THELABEL TESTNAME)
  TEST_TEMPLATE(${TESTNAME} ${ARGN})
  set_tests_properties(${TESTNAME} PROPERTIES LABELS ${THELABEL})
ENDMACRO()

#Currently, the STL test files are fetched from another project. Ideally, instead, they should be generated programmatically
#from as little binary data as possible (in principle, it is possible to generate similar files from around 1-5kb).
#However, this is not implemented yet, so we are stuck for now fetching the files from elsewhere. Anyway, as the files will
#eventually be generated, we do not copy them at generation time but at test time, so when we have the means to generate them
#programmatically, we can just swap the instances of TEST_COPY_FILE by another macro, but keep the dependency tree intact.
MACRO(TEST_COPY_FILE THELABEL TESTNAME ABSFILENAME)
  get_filename_component(FILENAME "${ABSFILENAME}" NAME)
  TEST_TEMPLATE_LABEL(${THELABEL} ${TESTNAME} "${OUTPUTDIR}" "${CMAKE_COMMAND}" -E copy_if_different "${DATATEST_DIR}/${FILENAME}" "${TEST_DIR}/${FILENAME}")
ENDMACRO()

#test to compare two files
MACRO(TEST_COMPARE TESTNAME THELABELS PREVTEST FILENAME1 FILENAME2)
  TEST_TEMPLATE(${TESTNAME} "${OUTPUTDIR}" "${CMAKE_COMMAND}" -E compare_files "${FILENAME1}" "${FILENAME2}")
  set_tests_properties(${TESTNAME} PROPERTIES LABELS "${THELABELS}" DEPENDS "${PREVTEST}" REQUIRED_FILES "${FILENAME1};${FILENAME2}" ${ARGN})
ENDMACRO()

#base macro to test the multires executable; highly generic, so it is somewhat cryptic...
MACRO(TEST_MULTIRES TESTNAME THELABELS_MULTIRES PREVTEST INPUTFILE ARGUMENTS)
  set(PARAMSFILE "${TEST_DIR}/${TESTNAME}.params")
  FILE(WRITE "${PARAMSFILE}" "${ARGUMENTS}")
  TEST_TEMPLATE(${TESTNAME} "${OUTPUTDIR}" ${multires} --response-file "${PARAMSFILE}")
  set_tests_properties(${TESTNAME} PROPERTIES LABELS "${THELABELS_MULTIRES}" DEPENDS "${PREVTEST}" REQUIRED_FILES "${INPUTFILE}")
ENDMACRO()

MACRO(TEST_MULTIRES_COMPARE PRODUCTFILENAME TESTNAME THELABELS_MULTIRES THELABELS_COMP PREVTEST INPUTFILE ARGUMENTS)
  if("${PRODUCTFILENAME}" STREQUAL "")
    set(PFILENAME "${TESTNAME}.paths")
  else()
    set(PFILENAME "${PRODUCTFILENAME}")
  endif()
  #for this to work, the output must be 
  set(PRODUCT "${TEST_DIR}/${PFILENAME}")
  TEST_MULTIRES(${TESTNAME} "${THELABELS_MULTIRES}" "${PREVTEST}" "${INPUTFILE}" "${ARGUMENTS}")
  set(PRODUCTPREV "${TESTPREV_DIR}/${PFILENAME}")
  TEST_COMPARE(COMPARE_${TESTNAME} "${THELABELS_COMP}" "${TESTNAME}" "${PRODUCT}" "${PRODUCTPREV}")
ENDMACRO()

MACRO(TEST_MULTICOMPARE TESTNAME THELABELS_COMP)
  SET(COUNT 0)
  FOREACH(FNAME ${ARGN})
    set(PRODUCT         "${TEST_DIR}/${FNAME}")
    set(PRODUCTPREV "${TESTPREV_DIR}/${FNAME}")
    TEST_COMPARE(COMPARE_${TESTNAME}_${COUNT} "${THELABELS_COMP}" "${TESTNAME}" "${PRODUCT}" "${PRODUCTPREV}")
  ENDFOREACH()
ENDMACRO()

#macro to repeat test/compare twice: once without snap, also with snap. To that end,
#it must be supplied with a name of a macro that adds snapping to the configuration
MACRO(TEST_MULTIRES_BOTHSNAP PRODUCTFILENAME TESTNAME THELABELS_MULTIRES THELABELS_COMP PREVTEST INPUTFILE COMMONARGS SNAPMACRONAME)
  TEST_MULTIRES_COMPARE("${PRODUCTFILENAME}" "${TESTNAME}_nosnap" "${THELABELS_MULTIRES}" "${THELABELS_COMP}" "${PREVTEST}" "${INPUTFILE}"
"--load ${INPUTFILE} --save \"${TEST_DIR}/${TESTNAME}_nosnap.paths\"
${COMMONARGS}")
  TEST_MULTIRES_COMPARE("${PRODUCTFILENAME}" "${TESTNAME}_snap"   "${THELABELS_MULTIRES}" "${THELABELS_COMP}" "${PREVTEST}" "${INPUTFILE}"
"--load ${INPUTFILE} --save \"${TEST_DIR}/${TESTNAME}_snap.paths\"
${COMMONARGS}
${${SNAPMACRONAME}}")
ENDMACRO()

#some tests are also used to test raw save/load
MACRO(TEST_MULTIRES_COMPARE_AND_TEST_RAW PRODUCTFILENAME TESTNAME THELABELS_MULTIRES THELABELS_COMP PREVTEST INPUTFILE COMMONARGS)
  #straight test
  TEST_MULTIRES_COMPARE("${PRODUCTFILENAME}" ${TESTNAME} ${THELABELS_MULTIRES} ${THELABELS_COMP} ${PREVTEST} "${INPUTFILE}"
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${COMMONARGS}")
  #raw save test
  TEST_MULTIRES_COMPARE("${PRODUCTFILENAME}" ${TESTNAME}_saveraw ${THELABELS_MULTIRES} ${THELABELS_COMP} ${PREVTEST} "${INPUTFILE}"
"--load \"${TEST_DIR}/full.stl\" --just-save-raw \"${TEST_DIR}/${TESTNAME}_saveraw.paths\"
${COMMONARGS}")
  #raw load test
  TEST_MULTIRES_COMPARE("${PRODUCTFILENAME}" ${TESTNAME}_loadraw ${THELABELS_MULTIRES} ${THELABELS_COMP} ${TESTNAME}_saveraw "${TEST_DIR}/${TESTNAME}_saveraw.paths"
"--load-raw \"${TEST_DIR}/${TESTNAME}_saveraw.paths\" --save \"${TEST_DIR}/${TESTNAME}_loadraw.paths\"
${COMMONARGS}")
  #this TEST_COMPARE is to check that the --load-raw and --just-save-raw flags work as intended 
  TEST_COMPARE(${TESTNAME}_compareraw ${THELABELS_MULTIRES} "${TESTNAME};${TESTNAME}_loadraw" "${TEST_DIR}/${TESTNAME}.paths" "${TEST_DIR}/${TESTNAME}_loadraw.paths")
ENDMACRO()

###########################################################
###### CUSTOM TEST TARGETS: check*
###########################################################

#tests are classified along two dimensions:
#    size:  mini/full     (mini==small/fast, full==big/slow)
#    stage: put/exec/comp (put==copy/generate test input file, exec==compute output file, comp==compare output file with previous output)

#this complex conditional is because when the generator is Visual Studio, CMAKE_BUILD_TYPE may be literally "" instead of the empty string...
if("${CMAKE_BUILD_TYPE}" STREQUAL "" OR "${CMAKE_BUILD_TYPE}" STREQUAL "\"\"")
  set(CBT "Release")
else()
  set(CBT "${CMAKE_BUILD_TYPE}")
endif()
set(CTEST_ARGS -C ${CBT} --output-on-failure)
if(USE_GCOV)
  #these commands work because they are executed in the build directory of the current subproject
  set(PRETEST  COMMAND ${LCOVPATH} --directory . --zerocounters)
  set(POSTTEST COMMAND ${LCOVPATH} --capture --directory . --output-file coverage.info
               COMMAND ${GENHTMLPATH} coverage.info --output-directory lcov_report)
else()
  set(PRETEST )
  set(POSTTEST )
endif()
add_custom_target(check          ${PRETEST} COMMAND ctest ${CTEST_ARGS}                  ${POSTTEST})
add_custom_target(checkmini      ${PRETEST} COMMAND ctest ${CTEST_ARGS} -L mini          ${POSTTEST})
add_custom_target(checkfull      ${PRETEST} COMMAND ctest ${CTEST_ARGS} -L full          ${POSTTEST})
add_custom_target(checkncomp     ${PRETEST} COMMAND ctest ${CTEST_ARGS}         -LE comp ${POSTTEST})
add_custom_target(checkncompmini ${PRETEST} COMMAND ctest ${CTEST_ARGS} -L mini -LE comp ${POSTTEST})
add_custom_target(checkncompfull ${PRETEST} COMMAND ctest ${CTEST_ARGS} -L full -LE comp ${POSTTEST})
add_custom_target(checkcomp      ${PRETEST} COMMAND ctest ${CTEST_ARGS} -L comp          ${POSTTEST})
add_custom_target(checkcompmini  ${PRETEST} COMMAND ctest ${CTEST_ARGS} -L compmini      ${POSTTEST})
add_custom_target(checkcompfull  ${PRETEST} COMMAND ctest ${CTEST_ARGS} -L compfull      ${POSTTEST})

#invaluable resource to understand the black art of custom commands/targets in cmake, if we want to move to a more complex testing model:
# https://samthursfield.wordpress.com/2015/11/21/cmake-dependencies-between-targets-and-files-and-custom-commands/
# http://stackoverflow.com/questions/733475/cmake-ctest-make-test-doesnt-build-tests
# https://cmake.org/cmake/help/v3.0/command/add_custom_target.html
# https://cmake.org/cmake/help/v3.0/command/add_custom_command.html
# http://stackoverflow.com/questions/15115075/how-to-run-ctest-after-building-my-project-with-cmake

###########################################################
###### DATA FILE COPY/GENERATION
###########################################################

#STL files to copy
set(MINISTL        "put_mini;${TEST_DIR}/mini.stl")
set(MINISALIENTSTL "put_mini_salient;${TEST_DIR}/mini.salient.stl")
set(FULLSTL        "put_full;${TEST_DIR}/full.stl")
set(FULLOUTERSTL   "put_full_outer;${TEST_DIR}/full.outer.stl")
set(FULLFATSTL     "put_full_fat;${TEST_DIR}/full.fat.stl")
set(TOWERSTL       "put_full_tower;${TEST_DIR}/tower.stl")
set(BIGRIDSTL      "put_full_bigrid;${TEST_DIR}/bigrid.stl")
set(FILTERSTL      "put_full_filter;${TEST_DIR}/filter.stl")
set(STAIRSASTL     "put_full_stairsA;${TEST_DIR}/stairs.A.stl")
set(STAIRSBSTL     "put_full_stairsB;${TEST_DIR}/stairs.B.stl")
set(SALIENTSTL     "put_full_justsalient;${TEST_DIR}/salient.stl")
set(FULLSALIENTSTL "put_full_salient;${TEST_DIR}/full.salient.stl")
set(FULLDENTEDSTL  "put_full_dented;${TEST_DIR}/third.dented.stl")
set(FULLSUBSTL     "put_full_subtractive;${TEST_DIR}/subtractive.stl")
TEST_COPY_FILE(putmini ${MINISTL})
TEST_COPY_FILE(putmini ${MINISALIENTSTL})
TEST_COPY_FILE(putfull ${FULLSTL})
TEST_COPY_FILE(putfull ${FULLOUTERSTL})
TEST_COPY_FILE(putfull ${FULLFATSTL})
TEST_COPY_FILE(putfull ${TOWERSTL})
TEST_COPY_FILE(putfull ${BIGRIDSTL})
TEST_COPY_FILE(putfull ${FILTERSTL})
TEST_COPY_FILE(putfull ${STAIRSASTL})
TEST_COPY_FILE(putfull ${STAIRSBSTL})
TEST_COPY_FILE(putfull ${SALIENTSTL})
TEST_COPY_FILE(putfull ${FULLDENTEDSTL})
TEST_COPY_FILE(putfull ${FULLSUBSTL})
#STL file to generate
TEST_TEMPLATE(put_full_salient "${OUTPUTDIR}/${PYTHONSCRIPTS_PATH}" "${PYTHON_EXECUTABLE}" "mergestls.py" "${TEST_DIR}/full.stl" "${TEST_DIR}/salient.stl" "${TEST_DIR}/full.salient.stl")
set_tests_properties(put_full_salient PROPERTIES
                     LABELS "putfull"
                     DEPENDS "put_full;put_full_justsalient"
                     REQUIRED_FILES "${TEST_DIR}/full.stl;${TEST_DIR}/salient.stl")

###########################################################
###### MINI TEST CASES
###########################################################

#NOTE: the configuration parameters of these tests have been tweaked to minimize computation time and output size;
#      they are not very good (--gridstep, --tolerances and --smoothing should always be significantly smaller than
#      --radx for quality output)

set(MINILABELS execmini compmini)
set(NOSCHED
"#configure the multislicer in simple (non-scheduling) mode
--slicing-uniform 0.1 --save-contours --motion-planner")
set(SCHED
"#configure the multislicer in scheduling mode
--slicing-scheduler   --save-contours --motion-planner")
set(MINI_DIMST0 "--process 0 --radx 75 --tolerances 15 1  --smoothing 0.1")
set(MINI_DIMST1 "--process 1 --radx 10 --tolerances 2 0.1 --smoothing 0.1")
set(MINI_SCHED0 "${MINI_DIMST0} --voxel-profile ellipsoid --voxel-z 75 67.5")
set(MINI_SCHED1 "${MINI_DIMST1} --voxel-profile ellipsoid --voxel-z 10 9")
set(CLRNCE "--clearance --medialaxis-radius 1.0 #--clearance configures the slicer so the toolpaths cannot overlap")
set(USEGRID "--process 0 --gridstep 0.1 --process 1 --gridstep 0.1")
set(SNAPTHICK
"${USEGRID}
--process 0 --snap --process 1 --snap #--snap configures the slicer to snap the contours to a grid whose step is defined with --gridstep")
set(SNAPTHIN
"${SNAPTHICK}
--process 0 --safestep --process 1 --safestep #--safestep configures the slicer to minimize polygon smoothing as much as possible before snapping to grid")

#when SNAPTHICK is used instead of SNAPTHIN, it is because --safestep creates problems while snapping to grid, for the configuration in that test

set(TESTNAME mini_no3d_clearance_noinfilling)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"${NOSCHED}
${MINI_DIMST0}
  ${CLRNCE}
${MINI_DIMST1}
  ${CLRNCE}"
SNAPTHIN)

set(TESTNAME mini_no3d_substractive_box)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"--load \"${TEST_DIR}/mini.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${NOSCHED}
--subtractive-box-mode 6000.0 5000.0
${MINI_DIMST0}
  ${CLRNCE}
${MINI_DIMST1}
${SNAPTHIN}")

set(TESTNAME mini_no3d_clearance_infillingconcentric)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"${NOSCHED}
${MINI_DIMST0}
  ${CLRNCE}
  --infill concentric --infill-medialaxis-radius 0.5
${MINI_DIMST1}
  ${CLRNCE}
  --infill concentric --infill-medialaxis-radius 0.5"
SNAPTHIN)

set(TESTNAME mini_no3d_clearance_infillinglines)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"${NOSCHED}
${MINI_DIMST0}
  ${CLRNCE}
  --infill linesh --infill-medialaxis-radius 0.5
${MINI_DIMST1}
  ${CLRNCE}
  --infill linesv --infill-medialaxis-radius 0.5"
SNAPTHIN)

set(TESTNAME mini_no3d_clearance_infillingrecursive)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"${NOSCHED}
${MINI_DIMST0}
  ${CLRNCE}
  --infill concentric --infill-medialaxis-radius 0.5 --infilling-recursive
${MINI_DIMST1}
  ${CLRNCE}
  --infill linesv --infill-medialaxis-radius 0.5"
SNAPTHIN)

set(TESTNAME mini_no3d_addsub_medialaxis)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"${NOSCHED} --addsub
${MINI_DIMST0} --medialaxis-radius 0.05 --infill linesh
${MINI_DIMST1} --medialaxis-radius 0.5 --infill linesh"
SNAPTHIN)

set(TESTNAME mini_no3d_addsub_negclosing_gradual)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"${NOSCHED} --addsub --neg-closing 40 --overwrite-gradual 0.9 0.5 0.4 0.6 0.2 0.8 0 1
${MINI_DIMST0} 
${MINI_DIMST1} --medialaxis-radius 0.5"
SNAPTHICK)

set(TESTNAME mini_dxf_justcontours)
TEST_MULTIRES_COMPARE("" ${TESTNAME} execmini compmini ${MINISTL}
"--load \"${TEST_DIR}/mini.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${NOSCHED}
--dxf-contours \"${TEST_DIR}/${TESTNAME}\" --dxf-format binary --dxf-by-tool #--dxf-by-z 
${MINI_DIMST0}
${MINI_DIMST1} --no-toolpaths --no-preprocessing 2
${USEGRID}")
TEST_COMPARE(COMPARE_DXF_mini_dxf_justcontours_N0 compmini ${TESTNAME}
      "${TEST_DIR}/${TESTNAME}.N0.dxf"
  "${TESTPREV_DIR}/${TESTNAME}.N0.dxf")
TEST_COMPARE(COMPARE_DXF_mini_dxf_justcontours_N1 compmini ${TESTNAME}
      "${TEST_DIR}/${TESTNAME}.N1.dxf"
  "${TESTPREV_DIR}/${TESTNAME}.N1.dxf")

set(TESTNAME mini_3d_clearance_noinfilling)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"${SCHED}
${MINI_SCHED0}
  ${CLRNCE}
${MINI_SCHED1}
  ${CLRNCE}"
SNAPTHIN)

set(TESTNAME mini_3d_clearance_infilling)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"${SCHED}
${MINI_SCHED0}
  ${CLRNCE}
  --infill linesh --infill-medialaxis-radius 0.5
${MINI_SCHED1}
  ${CLRNCE}
  --infill linesv --infill-medialaxis-radius 0.5"
SNAPTHIN)

set(TESTNAME mini_3d_infilling_addperimeters)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"${SCHED}
${MINI_SCHED0}
  --infill linesv --infill-medialaxis-radius 0.5 --additional-perimeters 1
${MINI_SCHED1}
  --infill linesh --infill-medialaxis-radius 0.5 --additional-perimeters 2 --additional-perimeters-lineoverlap 0.2"
SNAPTHIN)

set(TESTNAME mini_3d_infilling_withsurface)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"${SCHED}
${MINI_SCHED0}
  --infill linesvh --infill-medialaxis-radius 0.5
${MINI_SCHED1}
  --infill linesh --infill-static-mode --infill-lineoverlap -4 --surface-infill linesh --compute-surfaces-just-with-same-process false"
SNAPTHIN)

set(TESTNAME mini_3d_clearance_vcorrection)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISALIENTSTL}
"${SCHED} --vertical-correction
${MINI_SCHED0}
  ${CLRNCE}
  --infill linesh --infill-medialaxis-radius 0.5
${MINI_SCHED1}
  ${CLRNCE}
  --infill linesv --infill-medialaxis-radius 0.5"
SNAPTHIN)

set(TESTNAME mini_3d_clearance_infillingrec)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"${SCHED}
${MINI_SCHED0}
  ${CLRNCE}
  --infill linesh --infill-medialaxis-radius 0.5 --infilling-recursive
${MINI_SCHED1}
  ${CLRNCE}
  --infill linesv --infill-medialaxis-radius 0.5"
SNAPTHIN)

set(TESTNAME mini_3d_addsub)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"${SCHED} --addsub
${MINI_SCHED0}
  --medialaxis-radius 0.1 0.05
${MINI_SCHED1}
  --medialaxis-radius 0.5"
SNAPTHIN)

set(TESTNAME mini_3d_infilling_addsub)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${MINILABELS} ${MINISTL}
"${SCHED} --addsub
${MINI_SCHED0}
  --medialaxis-radius 0.1 0.05 --infill linesh
${MINI_SCHED1}
  --medialaxis-radius 0.5 --infill linesh"
SNAPTHIN)




###########################################################
###### FULL-FLEDGED TEST CASES
###########################################################

#these test cases aim to cover most functionality on the standalone executable, but for a codebase as complex as ours,
#with such a high cyclomatic complexity and such a high degree of coupling between many command-line options, it is 
#very difficult to test all relevant code paths. Even full coverage (even disregarding error handling and trivial edge
#cases) is difficult to achieve. Some command-line options are not tested at all, for many possible reasons: they would
#require user interaction, or do a very similar thing to another option, or are not a primary concern, or would require
#another style of testing to validate them, even taking into account the comparison tests.
#
#flags not tested:
#  standalone.cpp: --pp-save-in-grid --help --config --save-format --checkpoint-save-every --show --dry-run --dxf-toolpaths --dxf-separate-toolpaths --dxf-by-z
#  parsing.cpp: --correct-input --z-epsilon
#  parsing.cpp, nanoscribe section: --nano-by-tool --nano-by-z --nano-file-begin --pp-nano-file-begin --pp-nano-file-afterbegin --pp-nano-file-afterfirstzchange --nano-file-end --pp-nano-file-end --pp-nano-global-file-begin --nano-global-file-end --pp-nano-global-file-end --nano-perimeters-begin --pp-nano-perimeters-begin --nano-perimeters-end --pp-nano-perimeters-end --nano-surfaces-begin --pp-nano-surfaces-begin --nano-surfaces-end --pp-nano-surfaces-end --nano-infillings-begin --pp-nano-infillings-begin --nano-infillings-end --pp-nano-infillings-end --pp-nano-scanmode --nano-galvocenter --pp-nano-galvocenter --pp-nano-angle --pp-nano-spacing --pp-nano-margin --pp-nano-maxsquarelen --pp-nano-origin --pp-nano-gridstep
#  parsing.cpp, infill section: --infill-maxconcentric --surface-infill-maxconcentric --surface-infill-lineoverlap --surface-infill-byregion --surface-infill-static-mode --surface-infill-medialaxis-radius 
  

set(FULLLABELS execfull compfull)
set(NOSCHED
"#configure the multislicer in simple (non-scheduling) mode
--slicing-uniform 0.05 --save-contours --motion-planner")
set(FULL_DIMST0 "--process 0 --radx 75 --tolerances 0.75 0.01 --smoothing 0.1")
set(FULL_DIMST1 "--process 1 --radx 10 --tolerances 0.1  0.01 --smoothing 0.1")
set(FULL_SCHED0 "${FULL_DIMST0} --voxel-profile ellipsoid --voxel-z 75 67.5")
set(FULL_SCHED1 "${FULL_DIMST1} --voxel-profile ellipsoid --voxel-z 10 9")

TEST_MULTIRES_COMPARE_AND_TEST_RAW("" full_no3d_clearance_noinfilling ${FULLLABELS} ${FULLSTL}
"${NOSCHED}
${FULL_DIMST0}
  ${CLRNCE}
${FULL_DIMST1}
  ${CLRNCE}
${SNAPTHIN}")

set(TESTNAME full_no3d_save_in_grid)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}\"
${NOSCHED} --save-in-grid 3000 100
${FULL_DIMST0}
${FULL_DIMST1}
${SNAPTHICK}")
TEST_COMPARE(COMPARE_SAVEINGRID_${TESTNAME} compfull ${TESTNAME}
      "${TEST_DIR}/${TESTNAME}.N0.0.0.paths"
  "${TESTPREV_DIR}/${TESTNAME}.N0.0.0.paths"
  )

set(TESTNAME full_no3d_substractive_box)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${NOSCHED} --subtractive-box-mode 6000.0 5000.0
${FULL_DIMST0}
${FULL_DIMST1}
${SNAPTHICK}")

set(TESTNAME full_no3d_clearance_infillingconcentric)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${NOSCHED}
${FULL_DIMST0}
  ${CLRNCE}
  --infill concentric --infill-medialaxis-radius 0.5
${FULL_DIMST1}
  ${CLRNCE}
  --infill concentric --infill-medialaxis-radius 0.5
${SNAPTHIN}")

set(TESTNAME full_no3d_clearance_infillinglines)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"${NOSCHED}
${FULL_DIMST0}
  ${CLRNCE}
  --infill linesh --infill-medialaxis-radius 0.5
${FULL_DIMST1}
  ${CLRNCE}
  --infill linesv --infill-medialaxis-radius 0.5"
SNAPTHIN)

set(TESTNAME full_no3d_clearance_infillingrecursive)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${NOSCHED}
${FULL_DIMST0}
  ${CLRNCE}
  --infill concentric --infill-medialaxis-radius 0.5 --infilling-recursive
${FULL_DIMST1}
  ${CLRNCE}
  --infill linesh --infill-medialaxis-radius 0.5
${SNAPTHIN}")

set(TESTNAME full_no3d_clearance_infillingrecursive_removeredundant)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${NOSCHED}
${FULL_DIMST0}
  ${CLRNCE}
  --infill concentric --infill-medialaxis-radius 0.5 --infilling-recursive
${FULL_DIMST1}
  ${CLRNCE}
  --infill linesh --infill-medialaxis-radius 0.5 --radius-removecommon 0.1
  --infilling-recursive #technically, using recursive infilling in the last high-res process is unnecessary and wasteful, but it helps to boost code coverage
${SNAPTHIN}")

set(TESTNAME full_no3d_addsub_medialaxis)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${NOSCHED} --addsub
${FULL_DIMST0}
  --infill linesh --medialaxis-radius 0.05 
${FULL_DIMST1}
  --infill linesh --medialaxis-radius 0.5
${SNAPTHICK}")

set(TESTNAME full_no3d_addsub_negclosing_gradual)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLDENTEDSTL}
"--load \"${TEST_DIR}/third.dented.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${NOSCHED} --addsub --neg-closing 40 --overwrite-gradual 0.9 0.5 0.4 0.6 0.2 0.8 0 1
${FULL_DIMST0}
  --infill linesh
${FULL_DIMST1}
  --infill linesh --medialaxis-radius 0.5
${SNAPTHICK}")

set(TESTNAME full_dxf_justcontours)
TEST_MULTIRES_COMPARE("" ${TESTNAME} execfull compfull ${FULLSTL}
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${NOSCHED}
--dxf-contours \"${TEST_DIR}/${TESTNAME}\" --dxf-format binary --dxf-by-tool #--dxf-by-z 
${FULL_DIMST0}
${FULL_DIMST1}
  --no-toolpaths --no-preprocessing 2
${USEGRID}")
TEST_COMPARE(COMPARE_DXF_${TESTNAME}_N0 compfull ${TESTNAME}
      "${TEST_DIR}/${TESTNAME}.N0.dxf"
  "${TESTPREV_DIR}/${TESTNAME}.N0.dxf"
  )
TEST_COMPARE(COMPARE_DXF_${TESTNAME}_N1 compfull ${TESTNAME}
      "${TEST_DIR}/${TESTNAME}.N1.dxf"
  "${TESTPREV_DIR}/${TESTNAME}.N1.dxf"
  )

set(TESTNAME full_3d_clearance_noinfilling)
set(COMMONARGS
"${FULL_SCHED0}
  ${CLRNCE}
${FULL_SCHED1}
  ${CLRNCE}
${SNAPTHIN}")
TEST_MULTIRES_COMPARE_AND_TEST_RAW("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"${SCHED}
${COMMONARGS}")
  #checkpoint save test
  TEST_MULTIRES(${TESTNAME}_save_checkpoint execfull ${FULLSTL}
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}_checkpoint.paths\" --checkpoint-save \"${TEST_DIR}/${TESTNAME}.checkpoint\" 5
${SCHED}
${COMMONARGS}")
  #checkpoint load test
  TEST_MULTIRES(${TESTNAME}_load_checkpoint execfull ${TESTNAME}_save_checkpoint "${TEST_DIR}/full.stl;${TEST_DIR}/${TESTNAME}.checkpoint"
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}_checkpoint.paths\" --checkpoint-load \"${TEST_DIR}/${TESTNAME}.checkpoint\"
${SCHED}
${COMMONARGS}")
  #this comparison is not part of the comp* series, because that is intended to compare the results of *two* sets of results;
  #this TEST_COMPARE is to check that the --checkpoint-save and --checkpoint-load flags work as intended 
  TEST_COMPARE(${TESTNAME}_comparecheckpoint execfull "${TESTNAME};${TESTNAME}_load_checkpoint" "${TEST_DIR}/${TESTNAME}.paths" "${TEST_DIR}/${TESTNAME}_checkpoint.paths")
  #now do the same test, but with manually specified slices
  TEST_MULTIRES_COMPARE("" ${TESTNAME}_slicingmanual ${FULLLABELS} ${FULLSTL}
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}_slicingmanual.paths\"
--save-contours --motion-planner --slicing-manual
    #these slice heights should be the same as in the test ${TESTNAME} when executed in intel x86-64
    0 -0.0028361406922340392026
    1 -0.061336140692234038252
    1 -0.043336140692234043081
    1 -0.025336140692234040972
    1 -0.0073361406922340397299
    1 0.010663859307765961512
    1 0.028663859307765961887
    1 0.046663859307765960527
    1 0.064663859307765955697
${COMMONARGS}")
  #make sure that --slicing-manual produces the same results as --slicing-scheduler if all other parameters remain the same
  TEST_COMPARE(${TESTNAME}_compare_slicingmanual execfull ${TESTNAME}_slicingmanual "${TEST_DIR}/${TESTNAME}.paths" "${TEST_DIR}/${TESTNAME}_slicingmanual.paths")
#Test feedback with mesh
TEST_MULTIRES_COMPARE("" ${TESTNAME}_feedback_mesh ${FULLLABELS} "put_full;put_full_outer" "${TEST_DIR}/full.stl"
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}_feedback_mesh.paths\" --feedback mesh \"${TEST_DIR}/full.outer.stl\"
${SCHED}
${COMMONARGS}")
#To test the feedback with paths, first we will extract the toolpaths for the low-res tool
TEST_TEMPLATE(${TESTNAME}_prepare_feedback_paths "${OUTPUTDIR}" ${filterp} "${TEST_DIR}/${TESTNAME}.paths" "${TEST_DIR}/${TESTNAME}_prepare_feedback_paths.paths" type contour ntool 0 z -0.0028361406922340392026)
set_tests_properties(${TESTNAME}_prepare_feedback_paths PROPERTIES LABELS "execfull" DEPENDS "${TESTNAME}" REQUIRED_FILES "${TEST_DIR}/${TESTNAME}.paths")
#Now, to the proper test
TEST_MULTIRES_COMPARE("" ${TESTNAME}_feedback_paths ${FULLLABELS} "put_full;${TESTNAME}_prepare_feedback_paths" "${TEST_DIR}/full.stl"
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}_feedback_paths.paths\" --feedback paths \"${TEST_DIR}/${TESTNAME}_prepare_feedback_paths.paths\"
#In this test, we do just one high-res slice at the same height as the feedback, to avoid quite a bunch of tedious transformations with touchp and unionp
--save-contours --motion-planner --slicing-manual 1 -0.0028361406922340392026
${COMMONARGS}")

set(TESTNAME full_3d_substractive_box)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${SCHED} --subtractive-box-mode 6000.0 5000.0
${FULL_SCHED0}
${FULL_SCHED1}
${SNAPTHICK}")

set(TESTNAME full_3d_clearance_infillinglines)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${SCHED}
${FULL_SCHED0}
  ${CLRNCE}
  --infill linesh --infill-medialaxis-radius 0.5
${FULL_SCHED1}
  ${CLRNCE}
  --infill linesv --infill-medialaxis-radius 0.5
${SNAPTHIN}")

set(TESTNAME full_3d_infillinglines_addperimeters)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"${SCHED}
${FULL_SCHED0}
  --infill linesv --infill-medialaxis-radius 0.5 --additional-perimeters 1
${FULL_SCHED1}
  --infill linesh --infill-medialaxis-radius 0.5 --additional-perimeters 2 --additional-perimeters-lineoverlap 0.2"
SNAPTHICK)

MACRO(TEMPLATE_surfacesWithSameProcess TESTNAME DIFFERENCE)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLFATSTL}
"--load \"${TEST_DIR}/full.fat.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${SCHED}
${FULL_SCHED0}
  --infill linesvh --infill-medialaxis-radius 0.5
${FULL_SCHED1}
  --infill linesh --infill-static-mode --infill-lineoverlap -4 --surface-infill linesh
  --compute-surfaces-extent-factor 0.6 ${DIFFERENCE}")
ENDMACRO()
TEMPLATE_surfacesWithSameProcess(full_3d_withsurface_allprocesses     "--compute-surfaces-just-with-same-process true
${USEGRID}")
TEMPLATE_surfacesWithSameProcess(full_3d_withsurface_betweenprocesses "--compute-surfaces-just-with-same-process false
${USEGRID}")
TEMPLATE_surfacesWithSameProcess(full_3d_withsurface_lump_surf_to_per "--lump-surfaces-to-perimeters
${USEGRID}")
TEMPLATE_surfacesWithSameProcess(full_3d_withsurface_lump_surf_to_inf "--lump-surfaces-to-infillings
${USEGRID}")
TEMPLATE_surfacesWithSameProcess(full_3d_withsurface_lump_all         "--lump-all-toolpaths-together
${USEGRID}")
TEMPLATE_surfacesWithSameProcess(full_3d_clearance_withsurface_allprocesses "--medialaxis-radius 0.5 --compute-surfaces-just-with-same-process true
-- process 0 ${CLRNCE}
-- process 1 ${CLRNCE}
${SNAPTHICK}")
TEST_COMPARE(COMPARE_NOTEQUAL_withsurface_allprocesses_1 execfull "full_3d_withsurface_allprocesses;full_3d_withsurface_betweenprocesses"
  "${TEST_DIR}/full_3d_withsurface_allprocesses.paths"
  "${TEST_DIR}/full_3d_withsurface_betweenprocesses.paths"
  WILL_FAIL true) #the results MUST be different
TEST_COMPARE(COMPARE_NOTEQUAL_withsurface_allprocesses_2 execfull "full_3d_withsurface_lump_surf_to_per;full_3d_withsurface_lump_surf_to_inf"
  "${TEST_DIR}/full_3d_withsurface_lump_surf_to_per.paths"
  "${TEST_DIR}/full_3d_withsurface_lump_surf_to_inf.paths"
  WILL_FAIL true) #the results MUST be different
TEST_COMPARE(COMPARE_NOTEQUAL_withsurface_allprocesses_3 execfull "full_3d_withsurface_lump_surf_to_per;full_3d_withsurface_lump_all"
  "${TEST_DIR}/full_3d_withsurface_lump_surf_to_per.paths"
  "${TEST_DIR}/full_3d_withsurface_lump_all.paths"
  WILL_FAIL true) #the results MUST be different
TEST_COMPARE(COMPARE_NOTEQUAL_withsurface_allprocesses_4 execfull "full_3d_withsurface_lump_all;full_3d_withsurface_lump_surf_to_inf"
  "${TEST_DIR}/full_3d_withsurface_lump_all.paths"
  "${TEST_DIR}/full_3d_withsurface_lump_surf_to_inf.paths"
  WILL_FAIL true) #the results MUST be different
TEST_COMPARE(COMPARE_NOTEQUAL_withsurface_allprocesses_5 execfull "full_3d_withsurface_lump_all;full_3d_withsurface_allprocesses"
  "${TEST_DIR}/full_3d_withsurface_lump_all.paths"
  "${TEST_DIR}/full_3d_withsurface_allprocesses.paths"
  WILL_FAIL true) #the results MUST be different
#we do not write all combinations because it is pointless to be so exhaustive...
  
set(TESTNAME full_3d_clearance_vcorrection_infillinglines)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLSALIENTSTL}
"--load \"${TEST_DIR}/full.salient.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${SCHED} --vertical-correction
${FULL_SCHED0}
  ${CLRNCE}
  --infill linesh --infill-medialaxis-radius 0.5
${FULL_SCHED1}
  ${CLRNCE}
  --infill linesh --infill-medialaxis-radius 0.5
${SNAPTHIN}")

#no comparison with full_3d_clearance_vcorrection_infillinglines is done, because the results may have very small differences
set(TESTNAME full_3d_clearance_vcorrection_loadmulti)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} "put_full;put_full_justsalient" "${TEST_DIR}/full.stl;${TEST_DIR}/salient.stl"
"--load-multi \"${TEST_DIR}/full.stl\" \"${TEST_DIR}/salient.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${SCHED} --vertical-correction
${FULL_SCHED0}
  ${CLRNCE}
  --infill linesh --infill-medialaxis-radius 0.5
${FULL_SCHED1}
  ${CLRNCE}
  --infill linesh --infill-medialaxis-radius 0.5
${SNAPTHIN}")

set(TESTNAME full_3d_clearance_infillingrecursive)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${SCHED}
${FULL_SCHED0}
  ${CLRNCE}
  --infill linesh --infilling-recursive
${FULL_SCHED1}
  ${CLRNCE}
  --infill linesh
${SNAPTHIN}")

set(TESTNAME full_3d_addsub_infillinglines)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"--load \"${TEST_DIR}/full.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${SCHED} --addsub
${FULL_SCHED0}
  --medialaxis-radius 0.1 0.05 --infill linesh
${FULL_SCHED1}
  --medialaxis-radius 0.5      --infill linesh
${SNAPTHICK}")

set(TESTNAME full_3d_addsub)
TEST_MULTIRES_BOTHSNAP("" ${TESTNAME} ${FULLLABELS} ${FULLSTL}
"${SCHED} --addsub
${FULL_SCHED0}
  --medialaxis-radius 0.1 0.05
${FULL_SCHED1}
  --medialaxis-radius 0.5"
SNAPTHICK)

set(TESTNAME full_3d_voxel_spec)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FULLSUBSTL}
"--load \"${TEST_DIR}/subtractive.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${SCHED} --slicing-zbase 0 --slicing-direction down
--process 0 --voxel-profile interpolated --voxel-spec -36.5 50 -34 100 -31 123 -27 140 -20.5 157 -18.5 180 -13.5 206 3 256 10 285 15 300 41.5 362 --voxel-application-point 41.5 --voxel-zlimits -36.5 41.5
  --smoothing 0.1
--process 1 --voxel-profile interpolated --voxel-spec -4 4.9 0 4.9 --voxel-application-point 0 --voxel-zlimits -5 0
  --smoothing 0.1 --infill linesh --infill-medialaxis-radius 1.0 0.5
${SNAPTHIN}")


set(FULL_NANOA0 "--process 0 --radx 0.1 --voxel-profile constant --voxel-z 0.75 0.75   --always-preprocessing --smoothing 0.001 --tolerances 0.001 0.001")
set(FULL_NANOA1 "--process 1 --radx 0.1 --voxel-profile constant --voxel-z 0.375 0.375 --always-preprocessing --smoothing 0.001 --tolerances 0.001 0.001")

MACRO(TEMPLATE_bigrid_alwaysSupported TESTNAME DIFFERENCE)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${BIGRIDSTL}  
"--load \"${TEST_DIR}/bigrid.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${SCHED} --vertical-correction
${FULL_NANOA0}
  --infill linesavh ${DIFFERENCE}
${FULL_NANOA1}
  --infill linesavh")
ENDMACRO()
TEMPLATE_bigrid_alwaysSupported(full_3d_bigrid_alwaysSupported "--always-supported --always-supported-extent-factor 0.1")
TEMPLATE_bigrid_alwaysSupported(full_3d_bigrid_notalwaysSupported "")
TEST_COMPARE(COMPARE_NOTEQUAL_full_3d_bigrid_alwaysSupported execfull "full_3d_bigrid_alwaysSupported;full_3d_bigrid_notalwaysSupported"
  "${TEST_DIR}/full_3d_bigrid_alwaysSupported.paths"
  "${TEST_DIR}/full_3d_bigrid_notalwaysSupported.paths"
  WILL_FAIL true) #the results MUST be different

set(FULL_NANOA0 "--process 0 --radx 0.2 --voxel-profile constant --voxel-z 0.75 0.75   --always-preprocessing --smoothing 0.001 --tolerances 0.001 0.001")
set(FULL_NANOA1 "--process 1 --radx 0.1 --voxel-profile constant --voxel-z 0.375 0.375 --always-preprocessing --smoothing 0.001 --tolerances 0.001 0.001")
set(NANOCOMMONPARAMS
"--nano-global --nano-global-file-begin
\"%$scalingtool0 and $scalingtool1 determine the laser power for the low and high resolution toolpaths, respectively.
var $scalingtool0 = 1.45
var $scalingtool1 = 0.6
% $scalingtoolI is the scaling for the first layer. We know the first layer is low res, so its scaling should be $scalingtool0,
% but the interface reflects a sizable amount of the laser power back, so it ends up being irradiated at almost twice the power.
% This is a hack to avoid this problem.
var $scalingtoolI = 1 

ContinuousMode
ConnectPointsOn
TiltCorrectionOff
GalvoScanMode
ScanSpeed 10000
LaserPower 40
FindInterfaceAt 0
XOffset 0
YOffset 0
ZOffset -0.3
\"
--nano-file-afterbegin
\"AddZOffset -0.75
PowerScaling $scalingtoolI
\"
--nano-file-afterfirstzchange \"PowerScaling $scalingtool0 %afterfirstzchange\" --nano-gridstep 0.02
--process 0 --pp-nano-tool-begin \"PowerScaling $scalingtool0\" --pp-nano-tool-end \"%end PowerScaling $scalingtool0\"
--process 1 --pp-nano-tool-begin \"PowerScaling $scalingtool1\" --pp-nano-tool-end \"%end PowerScaling $scalingtool1\"")



set(TESTNAME full_nanoscribe_ensureAttachment)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${TOWERSTL}  
"--load \"${TEST_DIR}/tower.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${SCHED} --vertical-correction
${FULL_NANOA0}
  --infill linesavh --infill-static-mode --infilling-perimeter-overlap 0.45
${FULL_NANOA1}
  --infill linesv --medialaxis-radius 0.5  --infill-static-mode --infilling-perimeter-overlap 0.45 --ensure-attachment-offset 0.3 --ensure-attachment-cutoff-offset 0.095
--nanoscribe \"${TEST_DIR}/${TESTNAME}\"
#Since we are using GalvoScanMode in the global GWL file, we should also use --nano-scanmode galvo. We switch to piezo to test mode code paths
--nano-scanmode piezo
${NANOCOMMONPARAMS}
--nano-spacing 300 --nano-margin 0 --nano-maxsquarelen 300
")
TEST_COMPARE(COMPARE_GWL_${TESTNAME}_MASTER compfull ${TESTNAME}
      "${TEST_DIR}/${TESTNAME}.gwl"
  "${TESTPREV_DIR}/${TESTNAME}.gwl")
TEST_COMPARE(COMPARE_GWL_${TESTNAME}_SUB compfull ${TESTNAME}
      "${TEST_DIR}/${TESTNAME}.N0.0.0.gwl"
  "${TESTPREV_DIR}/${TESTNAME}.N0.0.0.gwl")

MACRO(TEMPLATE_startOverhangsOverSupport TESTNAME STLTEST STLNAME DIFFERENCE)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${STLTEST} ${STLNAME}  
"--load \"${STLNAME}\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${SCHED} --vertical-correction
${FULL_NANOA0}
  --infill linesavh --infill-static-mode
${FULL_NANOA1}
  --infill linesv --medialaxis-radius 0.5 ${DIFFERENCE}
--nanoscribe \"${TEST_DIR}/${TESTNAME}\" --nano-scanmode galvo ${NANOCOMMONPARAMS}
--nano-spacing 300 --nano-margin 0 --nano-maxsquarelen 300
")
ENDMACRO()
TEMPLATE_startOverhangsOverSupport(full_nanoscribe_NO_startOverhangsOverSupport ${STAIRSASTL} "")
TEMPLATE_startOverhangsOverSupport(full_nanoscribe_startOverhangsOverSupport ${STAIRSASTL}
  "--start-overhangs-over-support --start-overhangs-extent-factor 0.5 --start-overhangs-just-with-same-process false")
TEMPLATE_startOverhangsOverSupport(full_nanoscribe_startOverhangsOverSupport_notwork ${STAIRSASTL}
  "--start-overhangs-over-support --start-overhangs-extent-factor 0.5 --start-overhangs-just-with-same-process true")
TEMPLATE_startOverhangsOverSupport(full_nanoscribe_startOverhangsOverSupport_notfromedge ${STAIRSASTL}
  "--start-overhangs-over-support --start-overhangs-extent-factor 0.5 --start-overhangs-just-with-same-process false --overhangs-do-not-start-from-edge-of-support")
TEST_COMPARE(COMPARE_NOTEQUAL_full_nanoscribe_startOverhangsOverSupport_1 execfull "full_nanoscribe_startOverhangsOverSupport;full_nanoscribe_NO_startOverhangsOverSupport"
  "${TEST_DIR}/full_nanoscribe_startOverhangsOverSupport.paths"
  "${TEST_DIR}/full_nanoscribe_NO_startOverhangsOverSupport.paths"
  WILL_FAIL true) #the results MUST be different
TEST_COMPARE(COMPARE_NOTEQUAL_full_nanoscribe_startOverhangsOverSupport_2 execfull "full_nanoscribe_startOverhangsOverSupport;full_nanoscribe_startOverhangsOverSupport_notwork"
  "${TEST_DIR}/full_nanoscribe_startOverhangsOverSupport.paths"
  "${TEST_DIR}/full_nanoscribe_startOverhangsOverSupport_notwork.paths"
  WILL_FAIL true) #the results MUST be different
TEST_COMPARE(COMPARE_EQUAL_full_nanoscribe_startOverhangsOverSupport_3 execfull "full_nanoscribe_NO_startOverhangsOverSupport;full_nanoscribe_startOverhangsOverSupport_notwork"
  "${TEST_DIR}/full_nanoscribe_NO_startOverhangsOverSupport.paths"
  "${TEST_DIR}/full_nanoscribe_startOverhangsOverSupport_notwork.paths") #the results MUST be equal

#this test is not strictly necessary (except for a codepath activated when testing --infill lines* with --infill-byregion and without --infill-static-mode), but it is instructive to see slicings of more sophisticated shapes
set(TESTNAME full_nanoscribe_filter)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${FILTERSTL}  
"--load \"${TEST_DIR}/filter.stl\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${SCHED} --vertical-correction
${FULL_NANOA0}
  --infill linesavh --infill-byregion --infilling-perimeter-overlap 0.45
${FULL_NANOA1}
  --infill linesv --medialaxis-radius 0.5  --infill-byregion --infilling-perimeter-overlap 0.45
  --ensure-attachment-offset 0.3 --ensure-attachment-cutoff-offset 0.095
  --start-overhangs-over-support --start-overhangs-extent-factor 0.5 --overhangs-do-not-start-from-edge-of-support
--nanoscribe \"${TEST_DIR}/${TESTNAME}\" --nano-scanmode galvo ${NANOCOMMONPARAMS}
--nano-spacing 300 --nano-margin 0 --nano-maxsquarelen 300
")

MACRO(TEMPLATE_nanoscribe_multidomain TESTNAME STLTEST STLNAME DIFFERENCE)
TEST_MULTIRES_COMPARE("" ${TESTNAME} ${FULLLABELS} ${STLTEST} ${STLNAME}  
"--load \"${STLNAME}\" --save \"${TEST_DIR}/${TESTNAME}.paths\"
${SCHED} --vertical-correction
${FULL_NANOA0}
  --infill linesavh --infill-static-mode
${FULL_NANOA1}
  --infill linesv
--nanoscribe \"${TEST_DIR}/${TESTNAME}\" --nano-scanmode galvo ${NANOCOMMONPARAMS}
${DIFFERENCE}
")
ENDMACRO()
TEMPLATE_nanoscribe_multidomain(full_nanoscribe_multidomain_90 ${STAIRSBSTL} "--nano-spacing 67 --nano-margin 3 --nano-maxsquarelen 300")
TEMPLATE_nanoscribe_multidomain(full_nanoscribe_multidomain_60 ${STAIRSBSTL} "--nano-spacing 67 --nano-margin 3 --nano-maxsquarelen 300 --nano-angle 60")
TEMPLATE_nanoscribe_multidomain(full_nanoscribe_multidomain_90_abs ${STAIRSBSTL} "--nano-spacing 67 --nano-margin 3 --nano-maxsquarelen 300 --nano-origin 0 0")
TEST_COMPARE(COMPARE_EQUAL_full_nanoscribe_multidomain_1 execfull "full_nanoscribe_multidomain_90;full_nanoscribe_multidomain_60"
  "${TEST_DIR}/full_nanoscribe_multidomain_90.paths"
  "${TEST_DIR}/full_nanoscribe_multidomain_60.paths")
TEST_COMPARE(COMPARE_EQUAL_full_nanoscribe_multidomain_2 execfull "full_nanoscribe_multidomain_90;full_nanoscribe_multidomain_90_abs"
  "${TEST_DIR}/full_nanoscribe_multidomain_90.paths"
  "${TEST_DIR}/full_nanoscribe_multidomain_90_abs.paths")

