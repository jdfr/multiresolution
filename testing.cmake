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

MACRO(TEST_TEMPLATE TESTNAME WORKDIR)
  ADD_TEST(NAME ${TESTNAME}
           COMMAND ${ARGN}
           WORKING_DIRECTORY "${WORKDIR}")
ENDMACRO()

#Currently, the STL test files are fetched from another project. Ideally, instead, they should be generated programmatically
#from as little binary data as possible (in principle, it is possible to generate similar files from around 1-5kb).
#However, this is not implemented yet, so we are stuck for now fetching the files from elsewhere. Anyway, as the files will
#eventually be generated, we do not copy them at generation time but at test time, so when we have the means to generate them
#programmatically, we can just swap the instances of TEST_COPY_FILE by another macro, but keep the dependency tree intact.
MACRO(TEST_COPY_FILE TESTNAME FILENAME)
  TEST_TEMPLATE(${TESTNAME} "${OUTPUTDIR}" "${CMAKE_COMMAND}" -E copy_if_different "${DATATEST_DIR}/${FILENAME}" "${TEST_DIR}/${FILENAME}")
ENDMACRO()

MACRO(TEST_COMPARE TESTNAME THELABELS FILENAME1 FILENAME2)
  TEST_TEMPLATE(${TESTNAME} "${OUTPUTDIR}" "${CMAKE_COMMAND}" -E compare_files "${FILENAME1}" "${FILENAME2}")
  set_tests_properties(${TESTNAME} PROPERTIES LABELS "${THELABELS}" REQUIRED_FILES "${FILENAME1};${FILENAME2}")
ENDMACRO()

MACRO(TEST_MULTIRES TESTNAME THELABELS_MULTIRES THELABELS_COMP PREVTEST INPUTFILE)
  set(PRODUCT         "${TEST_DIR}/${TESTNAME}.paths")
  set(PRODUCTPREV "${TESTPREV_DIR}/${TESTNAME}.paths")
  TEST_TEMPLATE(${TESTNAME} "${OUTPUTDIR}" ${multires} --load "${TEST_DIR}/${INPUTFILE}" --save "${PRODUCT}" ${ARGN})
  TEST_COMPARE(COMPARE_${TESTNAME} "${THELABELS_COMP}" "${PRODUCT}" "${PRODUCTPREV}")
  set_tests_properties(${TESTNAME} PROPERTIES LABELS "${THELABELS_MULTIRES}" DEPENDS "${PREVTEST}" REQUIRED_FILES "${TEST_DIR}/${INPUTFILE}")
  set_tests_properties(COMPARE_${TESTNAME} PROPERTIES DEPENDS "${TESTNAME}")
ENDMACRO()

###########################################################
###### CUSTOM TEST TARGETS: check*
###########################################################

#tests are classified along two dimensions:
#    size:  mini/full     (mini==small/fast, full==big/slow)
#    stage: put/exec/comp (put==copy/generate test input file, exec==compute output file, comp==compare output file with previous output)

add_custom_target(check          COMMAND ctest --output-on-failure)
add_custom_target(checkmini      COMMAND ctest --output-on-failure -L mini)
add_custom_target(checkfull      COMMAND ctest --output-on-failure -L full)
add_custom_target(checkncomp     COMMAND ctest --output-on-failure         -LE comp)
add_custom_target(checkncompmini COMMAND ctest --output-on-failure -L mini -LE comp)
add_custom_target(checkncompfull COMMAND ctest --output-on-failure -L full -LE comp)
add_custom_target(checkcomp      COMMAND ctest --output-on-failure -L comp)
add_custom_target(checkcompmini  COMMAND ctest --output-on-failure -L compmini)
add_custom_target(checkcompfull  COMMAND ctest --output-on-failure -L compfull)

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
TEST_COPY_FILE(put_mini mini.stl)
TEST_COPY_FILE(put_mini_salient mini.salient.stl)
TEST_COPY_FILE(put_full full.stl)
TEST_COPY_FILE(put_full_dented full.dented.stl)
#STL file to generate
TEST_TEMPLATE(put_full_salient "${PYTHONSCRIPTS_PATH}" "${PYTHON_EXECUTABLE}" mergestls.py "${TEST_DIR}/full.stl" "${DATATEST_DIR}/salient.stl" "${TEST_DIR}/full.salient.stl")
set_tests_properties(put_mini put_mini_salient
                     PROPERTIES LABELS "putmini")
set_tests_properties(put_full put_full_dented put_full_salient
                     PROPERTIES LABELS "putfull")
set_tests_properties(put_full_salient PROPERTIES
                     DEPENDS "put_full;put_full_dented"
                     REQUIRED_FILES "${TEST_DIR}/full.stl;${DATATEST_DIR}/salient.stl")

###########################################################
###### MINI TEST CASES
###########################################################

#NOTE: the configuration parameters of these tests have been tweaked to minimize computation time and output size;
#      they are not very good (--gridstep, --tolerances and --smoothing should always be significantly smaller than
#      --radx for quality output)

set(MINILABELS execmini compmini)
set(MINISTL        put_mini         mini.stl)
set(MINISALIENTSTL put_mini_salient mini.salient.stl)
set(NOSCHED --slicing-uniform 0.1 --save-contours --motion-planner)
set(SCHED   --slicing-scheduler   --save-contours --motion-planner)
set(MINI_DIMST0 --process 0 --radx 75 --gridstep 0.1 --tolerances 15 1  --smoothing 0.1)
set(MINI_DIMST1 --process 1 --radx 10 --gridstep 0.1 --tolerances 2 0.1 --smoothing 0.1)
set(MINI_SCHED0 --voxel-profile ellipsoid --voxel-z 75 67.5)
set(MINI_SCHED1 --voxel-profile ellipsoid --voxel-z 10 9)
set(CLRNCE --safestep --snap --clearance --medialaxis-radius 1.0)

TEST_MULTIRES(mini_no3d_clearance_noinfilling ${MINILABELS} ${MINISTL}
              ${NOSCHED}
              ${MINI_DIMST0} ${CLRNCE}
              ${MINI_DIMST1} ${CLRNCE}
              )
TEST_MULTIRES(mini_no3d_clearance_infillingconcentric ${MINILABELS} ${MINISTL}
              ${NOSCHED} 
              ${MINI_DIMST0} ${CLRNCE} --infill concentric --infill-medialaxis-radius 0.5
              ${MINI_DIMST1} ${CLRNCE} --infill concentric --infill-medialaxis-radius 0.5
              )
TEST_MULTIRES(mini_no3d_clearance_infillinglines ${MINILABELS} ${MINISTL}
              ${NOSCHED} 
              ${MINI_DIMST0} ${CLRNCE} --infill linesh --infill-medialaxis-radius 0.5
              ${MINI_DIMST1} ${CLRNCE} --infill linesv --infill-medialaxis-radius 0.5
              )
TEST_MULTIRES(mini_no3d_clearance_infillingrecursive ${MINILABELS} ${MINISTL}
              ${NOSCHED} 
              ${MINI_DIMST0} ${CLRNCE} --infill concentric --infill-medialaxis-radius 0.5 --infilling-recursive
              ${MINI_DIMST1} ${CLRNCE} --infill linesv --infill-medialaxis-radius 0.5
              )
TEST_MULTIRES(mini_no3d_addsub_medialaxis ${MINILABELS} ${MINISTL}
              ${NOSCHED} --addsub 
              ${MINI_DIMST0} --safestep --snap --medialaxis-radius 0.05 --infill linesh
              ${MINI_DIMST1} --safestep --snap --medialaxis-radius 0.5  --infill linesh
              )
TEST_MULTIRES(mini_no3d_addsub_negclosing_gradual ${MINILABELS} ${MINISTL}
              ${NOSCHED} --addsub --neg-closing 40 --overwrite-gradual 0.9 0.5 0.4 0.6 0.2 0.8 0 1
              ${MINI_DIMST0} --snap 
              ${MINI_DIMST1} --snap --medialaxis-radius 0.5
              )
TEST_MULTIRES(mini_dxf_justcontours ${MINILABELS} ${MINISTL}
              ${NOSCHED} --dxf-contours "${TEST_DIR}/mini_dxf_justcontours" --dxf-format binary --dxf-by-tool #--dxf-by-z 
              ${MINI_DIMST0} 
              ${MINI_DIMST1} --no-toolpaths --no-preprocessing 2
              )
TEST_COMPARE(COMPARE_DXF_mini_dxf_justcontours_N0 compmini
                  "${TEST_DIR}/mini_dxf_justcontours.N0.dxf"
              "${TESTPREV_DIR}/mini_dxf_justcontours.N0.dxf"
              )
TEST_COMPARE(COMPARE_DXF_mini_dxf_justcontours_N1 compmini
                  "${TEST_DIR}/mini_dxf_justcontours.N1.dxf"
              "${TESTPREV_DIR}/mini_dxf_justcontours.N1.dxf"
              )
TEST_MULTIRES(mini_3d_clearance_noinfilling ${MINILABELS} ${MINISTL}
              ${SCHED}
              ${MINI_DIMST0} ${MINI_SCHED0} ${CLRNCE}
              ${MINI_DIMST1} ${MINI_SCHED1} ${CLRNCE}
              )
TEST_MULTIRES(mini_3d_clearance_infilling ${MINILABELS} ${MINISTL}
              ${SCHED}
              ${MINI_DIMST0} ${MINI_SCHED0} ${CLRNCE} --infill linesh --infill-medialaxis-radius 0.5
              ${MINI_DIMST1} ${MINI_SCHED1} ${CLRNCE} --infill linesv --infill-medialaxis-radius 0.5
              )
TEST_MULTIRES(mini_3d_infilling_addperimeters ${MINILABELS} ${MINISTL}
              ${SCHED}
              ${MINI_DIMST0} ${MINI_SCHED0} --safestep --snap --infill linesv --infill-medialaxis-radius 0.5 --additional-perimeters 1
              ${MINI_DIMST1} ${MINI_SCHED1} --safestep --snap --infill linesh --infill-medialaxis-radius 0.5 --additional-perimeters 2
              )
TEST_MULTIRES(mini_3d_infilling_withsurface ${MINILABELS} ${MINISTL}
              ${SCHED}
              ${MINI_DIMST0} ${MINI_SCHED0} --safestep --snap --infill linesvh --infill-medialaxis-radius 0.5
              ${MINI_DIMST1} ${MINI_SCHED1} --safestep --snap --infill linesh --infill-static-mode --infill-lineoverlap -4 --surface-infill linesh --compute-surfaces-just-with-same-process false
              )
TEST_MULTIRES(mini_3d_clearance_vcorrection ${MINILABELS} ${MINISALIENTSTL}
              ${SCHED} --vertical-correction
              ${MINI_DIMST0} ${MINI_SCHED0} ${CLRNCE} --infill linesh --infill-medialaxis-radius 0.5
              ${MINI_DIMST1} ${MINI_SCHED1} ${CLRNCE} --infill linesv --infill-medialaxis-radius 0.5
              )
TEST_MULTIRES(mini_3d_clearance_infillingrec ${MINILABELS} ${MINISTL}
              ${SCHED}
              ${MINI_DIMST0} ${MINI_SCHED0} ${CLRNCE} --infill linesh --infill-medialaxis-radius 0.5 --infilling-recursive
              ${MINI_DIMST1} ${MINI_SCHED1} ${CLRNCE} --infill linesv --infill-medialaxis-radius 0.5
              )
TEST_MULTIRES(mini_3d_addsub ${MINILABELS} ${MINISTL}
              ${SCHED} --addsub
              ${MINI_DIMST0} ${MINI_SCHED0} --safestep --snap --medialaxis-radius 0.1 0.05
              ${MINI_DIMST1} ${MINI_SCHED1} --safestep --snap --medialaxis-radius 0.5
              )
TEST_MULTIRES(mini_3d_infilling_addsub ${MINILABELS} ${MINISTL}
              ${SCHED} --addsub
              ${MINI_DIMST0} ${MINI_SCHED0} --safestep --snap --medialaxis-radius 0.1 0.05 --infill linesh
              ${MINI_DIMST1} ${MINI_SCHED1} --safestep --snap --medialaxis-radius 0.5 --infill linesh 
              )



