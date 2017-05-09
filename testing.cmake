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

#PRETTY PRINTER FOR OPTION FILES.
#
# So, why all this trouble to pretty-print argument lists?
# Wouldn't it be far easier to just have the argument files separately?
# Several reasons: huge amounts of repetition, parametrization, and having all of it centralized here in this file.
#
# BEWARE:
#   * ARGN SHOULD NOT BE EMPTY!!!
#   * semicolons cannot be included (they will be replaced by spaces, even if we do not use this sophisticated pretty printer
#   * arguments cannot contain double quotes (it may be possible to include them with some kind of escaping, but just keep it simple, OK?)
#   * comments (strings prefixed with #) MUST NOT be multiline
#   * comments cannot contain semicolons, either.
#   * comments cannot be inserted between an options and its arguments, or between several option arguments. Only between options or an option's final argument and the next option
MACRO(WRITE_ARG_FILE FILENAME)
  #--process options are unindented
  string(REPLACE ";--process" "\"\n\"--process" ARGUMENTS "${ARGN}")
  #any other option is indented (ideally, we should unindent global options, but it is just easier this way)
  string(REPLACE ";--" "\"\n    \"--" ARGUMENTS "${ARGUMENTS}")
  #comments are also indented
  string(REPLACE ";#" "\"\n    \"#" ARGUMENTS "${ARGUMENTS}")
  #option arguments go after its option
  string(REPLACE ";" "\" \"" ARGUMENTS "${ARGUMENTS}")
  #add initial and final double quotes (this is the reason ARGN should not be empty!!!)
  set(ARGUMENTS "    \"${ARGUMENTS}\"")
  #remove double quotes surrounding comments
  #string(REGEX REPLACE "\"#([^\"]*)\" " "#\\1\n" ARGUMENTS "${ARGUMENTS}") #this would be necessary to avoid problems if comments are allowed before the end of option arguments...
  string(REGEX REPLACE "\"#([^\"]*)\"" "#\\1" ARGUMENTS "${ARGUMENTS}")
  #double quotes are necessary only for tokens with witespace. Remove unnecessary ones
  string(REGEX REPLACE "\"([^ \"]+)\"" "\\1" ARGUMENTS "${ARGUMENTS}")
  FILE(WRITE "${FILENAME}" "${ARGUMENTS}")
ENDMACRO()

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

MACRO(TEST_TEMPLATE_LABEL THELABEL TESTNAME)
  TEST_TEMPLATE(${TESTNAME} ${ARGN})
  set_tests_properties(${TESTNAME} PROPERTIES LABELS ${THELABEL})
ENDMACRO()

#Currently, the STL test files are fetched from another project. Ideally, instead, they should be generated programmatically
#from as little binary data as possible (in principle, it is possible to generate similar files from around 1-5kb).
#However, this is not implemented yet, so we are stuck for now fetching the files from elsewhere. Anyway, as the files will
#eventually be generated, we do not copy them at generation time but at test time, so when we have the means to generate them
#programmatically, we can just swap the instances of TEST_COPY_FILE by another macro, but keep the dependency tree intact.
MACRO(TEST_COPY_FILE THELABEL TESTNAME FILENAME)
  TEST_TEMPLATE_LABEL(${THELABEL} ${TESTNAME} "${OUTPUTDIR}" "${CMAKE_COMMAND}" -E copy_if_different "${DATATEST_DIR}/${FILENAME}" "${TEST_DIR}/${FILENAME}")
ENDMACRO()

#test to compare two files
MACRO(TEST_COMPARE TESTNAME THELABELS PREVTEST FILENAME1 FILENAME2)
  TEST_TEMPLATE(${TESTNAME} "${OUTPUTDIR}" "${CMAKE_COMMAND}" -E compare_files "${FILENAME1}" "${FILENAME2}")
  set_tests_properties(${TESTNAME} PROPERTIES LABELS "${THELABELS}" DEPENDS "${PREVTEST}" REQUIRED_FILES "${FILENAME1};${FILENAME2}")
ENDMACRO()

#base macro to test the multires executable; highly generic, so it is somewhat cryptic... Best understood if you see how it is called by other macros
MACRO(TEST_MULTIRES PRODUCT LOADFLAG SAVEFLAG PREVTEST INPUTFILE TESTNAME THELABELS_MULTIRES)
  #the following is a hack to have automatically generated argument files in the test directory,
  #for didactic purposes.By commenting the following lines and uncommenting the commented
  #TEST_TEMPLATE, the code can be reverted to put all arguments inside add_test commands in the
  #file CTestTestfile.cmake
  set(PARAMSFILE "${TEST_DIR}/${TESTNAME}.params.txt")
  WRITE_ARG_FILE("${PARAMSFILE}" ${LOADFLAG} "${TEST_DIR}/${INPUTFILE}" ${SAVEFLAG} "${PRODUCT}" ${ARGN})
  TEST_TEMPLATE(${TESTNAME} "${OUTPUTDIR}" ${multires} --response-file "${PARAMSFILE}")
  #old, simple way: just shove all arguments into add_test
  #TEST_TEMPLATE(${TESTNAME} "${OUTPUTDIR}" ${multires} ${LOADFLAG} "${TEST_DIR}/${INPUTFILE}" ${SAVEFLAG} "${PRODUCT}" ${ARGN})
  set_tests_properties(${TESTNAME} PROPERTIES LABELS "${THELABELS_MULTIRES}" DEPENDS "${PREVTEST}" REQUIRED_FILES "${TEST_DIR}/${INPUTFILE}")
ENDMACRO()

#complement to TEST_MULTIRES to add a test comparison of the output of the executable with the corresponding output from another test run
MACRO(TEST_MULTIRES_COMPARE_CHOOSEFLAGS LOADFLAG SAVEFLAG PREVTEST INPUTFILE TESTNAME THELABELS_MULTIRES THELABELS_COMP)
  set(PRODUCT "${TEST_DIR}/${TESTNAME}.paths")
  TEST_MULTIRES(${PRODUCT} ${LOADFLAG} ${SAVEFLAG} ${PREVTEST} ${INPUTFILE} ${TESTNAME} ${THELABELS_MULTIRES} ${ARGN})
  set(PRODUCTPREV "${TESTPREV_DIR}/${TESTNAME}.paths")
  TEST_COMPARE(COMPARE_${TESTNAME} "${THELABELS_COMP}" "${TESTNAME}" "${PRODUCT}" "${PRODUCTPREV}")
ENDMACRO()

MACRO(TEST_MULTIRES_COMPARE)
  TEST_MULTIRES_COMPARE_CHOOSEFLAGS(--load --save ${ARGN})
ENDMACRO()

#some tests are also used to test raw save/load
MACRO(TEST_MULTIRES_COMPARE_TESTRAW PREVTEST INPUTFILE TESTNAME THELABELS_MULTIRES THELABELS_COMP)
  set(OARGS ${THELABELS_MULTIRES} ${THELABELS_COMP} ${ARGN})
  #raw save test
  TEST_MULTIRES_COMPARE_CHOOSEFLAGS(--load     --just-save-raw ${PREVTEST}         ${INPUTFILE}              ${TESTNAME}_saveraw ${OARGS})
  #raw load test
  TEST_MULTIRES_COMPARE_CHOOSEFLAGS(--load-raw --save          ${TESTNAME}_saveraw ${TESTNAME}_saveraw.paths ${TESTNAME}_loadraw ${OARGS})
  #this comparison is not part of the comp* series, because that is intended to compare the results of *two* sets of results;
  #this TEST_COMPARE is to check that the --load-raw and --just-save-raw flags work as intended 
  TEST_COMPARE(${TESTNAME}_compareraw ${THELABELS_MULTIRES} "${TESTNAME};${TESTNAME}_loadraw" "${TEST_DIR}/${TESTNAME}.paths" "${TEST_DIR}/${TESTNAME}_loadraw.paths")
ENDMACRO()

#some tests are also used to test load-multi
#for --load-multi, we do not do comparisons with previous sets of results
MACRO(TEST_MULTIRES_COMPARE_TESTLOADMULTI PREVTEST1 INPUTFILE1 PREVTEST2 INPUTFILE2 TESTNAME THELABELS_MULTIRES THELABELS_COMP)
  set(PRODUCT_ORIG "${TEST_DIR}/${TESTNAME}.paths")
  set(PRODUCT_LD   "${TEST_DIR}/${TESTNAME}_loadmulti.paths")
  TEST_TEMPLATE(${TESTNAME}_loadmulti "${OUTPUTDIR}" ${multires} --load-multi "${TEST_DIR}/${INPUTFILE1}" "${TEST_DIR}/${INPUTFILE2}" --save ${PRODUCT_LD} ${ARGN})
  set_tests_properties(${TESTNAME}_loadmulti PROPERTIES LABELS "${THELABELS_MULTIRES}" DEPENDS "${PREVTEST1};${PREVTEST2}" REQUIRED_FILES "${TEST_DIR}/${INPUTFILE1};${TEST_DIR}/${INPUTFILE2}")
  set(PRODUCTPREV "${TESTPREV_DIR}/${TESTNAME}_loadmulti.paths")
  TEST_COMPARE(COMPARE_${TESTNAME}_loadmulti "${THELABELS_COMP}" "${TESTNAME}_loadmulti" "${PRODUCT_LD}" "${PRODUCTPREV}")
  #no comparison with the original is done, because the results may have very small differences
  # #this comparison is not part of the comp* series, because that is intended to compare the results of *two* sets of results;
  # #this TEST_COMPARE is to check that the --checkpoint-save and --checkpoint-load flags work as intended 
  # TEST_COMPARE(${TESTNAME}_compare_loadmulti ${THELABELS_MULTIRES} "${TESTNAME};${TESTNAME}_loadmulti" ${PRODUCT_ORIG} ${PRODUCT_LD})
ENDMACRO()

#some tests are also used to test checkpointing
#for checkpointing, we do not do comparisons with previous sets of results
MACRO(TEST_MULTIRES_TESTCHECKPOINTING PREVTEST INPUTFILE TESTNAME THELABELS_MULTIRES THELABELS_COMP)
  #THELABELS_COMP is not used in this macro, but it is included to have the same interface as TEST_MULTIRES_COMPARE* macros, so this macro can share argument lists easily)
  set(PRODUCT_ORIG  "${TEST_DIR}/${TESTNAME}.paths")
  set(PRODUCT_CHKPT "${TEST_DIR}/${TESTNAME}_checkpoint.paths")
  set(CHKPT_FILE    "${TEST_DIR}/${TESTNAME}.checkpoint")
  #checkpoint save test
  TEST_MULTIRES(${PRODUCT_CHKPT} --load --save ${PREVTEST}                 ${INPUTFILE} ${TESTNAME}_save_checkpoint ${THELABELS_MULTIRES} ${ARGN} --checkpoint-save ${CHKPT_FILE} 5)
  #checkpoint load test
  TEST_MULTIRES(${PRODUCT_CHKPT} --load --save ${TESTNAME}_save_checkpoint ${INPUTFILE} ${TESTNAME}_load_checkpoint ${THELABELS_MULTIRES} ${ARGN} --checkpoint-load ${CHKPT_FILE})
  #this comparison is not part of the comp* series, because that is intended to compare the results of *two* sets of results;
  #this TEST_COMPARE is to check that the --checkpoint-save and --checkpoint-load flags work as intended 
  TEST_COMPARE(${TESTNAME}_comparecheckpoint ${THELABELS_MULTIRES} "${TESTNAME};${TESTNAME}_load_checkpoint" ${PRODUCT_ORIG} ${PRODUCT_CHKPT})
ENDMACRO()

#macro to repeat test/compare twice: once without snap, also with snap. To that end,
#it must be supplied with a name of a macro that adds snapping to the configuration
MACRO(TEST_MULTIRES_BOTHSNAP SNAPMACRONAME PREVTEST INPUTFILE TESTNAME)
  TEST_MULTIRES_COMPARE(${PREVTEST} ${INPUTFILE} ${TESTNAME}_nosnap ${ARGN})
  TEST_MULTIRES_COMPARE(${PREVTEST} ${INPUTFILE} ${TESTNAME}_snap   ${ARGN} ${${SNAPMACRONAME}})
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
add_custom_target(check          COMMAND ctest ${CTEST_ARGS})
add_custom_target(checkmini      COMMAND ctest ${CTEST_ARGS} -L mini)
add_custom_target(checkfull      COMMAND ctest ${CTEST_ARGS} -L full)
add_custom_target(checkncomp     COMMAND ctest ${CTEST_ARGS}         -LE comp)
add_custom_target(checkncompmini COMMAND ctest ${CTEST_ARGS} -L mini -LE comp)
add_custom_target(checkncompfull COMMAND ctest ${CTEST_ARGS} -L full -LE comp)
add_custom_target(checkcomp      COMMAND ctest ${CTEST_ARGS} -L comp)
add_custom_target(checkcompmini  COMMAND ctest ${CTEST_ARGS} -L compmini)
add_custom_target(checkcompfull  COMMAND ctest ${CTEST_ARGS} -L compfull)

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
set(MINISTL        put_mini             mini.stl)
set(MINISALIENTSTL put_mini_salient     mini.salient.stl)
set(FULLSTL        put_full             full.stl)
set(SALIENTSTL     put_full_justsalient salient.stl)
set(FULLSALIENTSTL put_full_salient     full.salient.stl)
set(FULLDENTEDSTL  put_full_dented      third.dented.stl)
set(FULLSUBSTL     put_full_subtractive subtractive.stl)
TEST_COPY_FILE(putmini ${MINISTL})
TEST_COPY_FILE(putmini ${MINISALIENTSTL})
TEST_COPY_FILE(putfull ${FULLSTL})
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
set(NOSCHED --slicing-uniform 0.1 --save-contours --motion-planner)
set(SCHED   --slicing-scheduler   --save-contours --motion-planner)
set(MINI_DIMST0 --process 0 --radx 75 --tolerances 15 1  --smoothing 0.1)
set(MINI_DIMST1 --process 1 --radx 10 --tolerances 2 0.1 --smoothing 0.1)
set(MINI_SCHED0 --voxel-profile ellipsoid --voxel-z 75 67.5)
set(MINI_SCHED1 --voxel-profile ellipsoid --voxel-z 10 9)
set(CLRNCE --clearance --medialaxis-radius 1.0)
set(USEGRID --process 0 --gridstep 0.1 --process 1 --gridstep 0.1)
set(SNAPTHICK ${USEGRID} --process 0 --snap --process 1 --snap)
set(SNAPTHIN ${SNAPTHICK} --process 0 --safestep --process 1 --safestep)

#when SNAPTHICK is used instead of SNAPTHIN, it is because --safestep creates problems while snapping to grid, for the configuration in that test

TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${MINISTL} mini_no3d_clearance_noinfilling
  ${MINILABELS} ${NOSCHED}
  ${MINI_DIMST0} ${CLRNCE}
  ${MINI_DIMST1} ${CLRNCE}
  )
TEST_MULTIRES_COMPARE(${MINISTL} mini_no3d_substractive_box
  ${MINILABELS} ${NOSCHED} ${SNAPTHIN} --subtractive-box-mode 6000.0 5000.0
  ${MINI_DIMST0} ${CLRNCE}
  ${MINI_DIMST1}
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${MINISTL} mini_no3d_clearance_infillingconcentric
  ${MINILABELS} ${NOSCHED}
  ${MINI_DIMST0} ${CLRNCE} --infill concentric --infill-medialaxis-radius 0.5
  ${MINI_DIMST1} ${CLRNCE} --infill concentric --infill-medialaxis-radius 0.5
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${MINISTL} mini_no3d_clearance_infillinglines
  ${MINILABELS} ${NOSCHED}
  ${MINI_DIMST0} ${CLRNCE} --infill linesh --infill-medialaxis-radius 0.5
  ${MINI_DIMST1} ${CLRNCE} --infill linesv --infill-medialaxis-radius 0.5
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${MINISTL} mini_no3d_clearance_infillingrecursive
  ${MINILABELS} ${NOSCHED}
  ${MINI_DIMST0} ${CLRNCE} --infill concentric --infill-medialaxis-radius 0.5 --infilling-recursive
  ${MINI_DIMST1} ${CLRNCE} --infill linesv --infill-medialaxis-radius 0.5
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${MINISTL} mini_no3d_addsub_medialaxis
  ${MINILABELS} ${NOSCHED} --addsub
  ${MINI_DIMST0} --medialaxis-radius 0.05 --infill linesh
  ${MINI_DIMST1} --medialaxis-radius 0.5  --infill linesh
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHICK ${MINISTL} mini_no3d_addsub_negclosing_gradual
  ${MINILABELS}
  ${NOSCHED} --addsub --neg-closing 40 --overwrite-gradual 0.9 0.5 0.4 0.6 0.2 0.8 0 1
  ${MINI_DIMST0}
  ${MINI_DIMST1} --medialaxis-radius 0.5
  )
TEST_MULTIRES_COMPARE(${MINISTL} mini_dxf_justcontours
  ${MINILABELS}
  ${USEGRID} ${NOSCHED} --dxf-contours "${TEST_DIR}/mini_dxf_justcontours" --dxf-format binary --dxf-by-tool #--dxf-by-z 
  ${MINI_DIMST0} 
  ${MINI_DIMST1} --no-toolpaths --no-preprocessing 2
  )
TEST_COMPARE(COMPARE_DXF_mini_dxf_justcontours_N0 compmini mini_dxf_justcontours
      "${TEST_DIR}/mini_dxf_justcontours.N0.dxf"
  "${TESTPREV_DIR}/mini_dxf_justcontours.N0.dxf"
  )
TEST_COMPARE(COMPARE_DXF_mini_dxf_justcontours_N1 compmini mini_dxf_justcontours
      "${TEST_DIR}/mini_dxf_justcontours.N1.dxf"
  "${TESTPREV_DIR}/mini_dxf_justcontours.N1.dxf"
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${MINISTL} mini_3d_clearance_noinfilling
  ${MINILABELS} ${SCHED}
  ${MINI_DIMST0} ${MINI_SCHED0} ${CLRNCE}
  ${MINI_DIMST1} ${MINI_SCHED1} ${CLRNCE}
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${MINISTL} mini_3d_clearance_infilling
  ${MINILABELS} ${SCHED}
  ${MINI_DIMST0} ${MINI_SCHED0} ${CLRNCE} --infill linesh --infill-medialaxis-radius 0.5
  ${MINI_DIMST1} ${MINI_SCHED1} ${CLRNCE} --infill linesv --infill-medialaxis-radius 0.5
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${MINISTL} mini_3d_infilling_addperimeters
  ${MINILABELS} ${SCHED}
  ${MINI_DIMST0} ${MINI_SCHED0} --infill linesv --infill-medialaxis-radius 0.5 --additional-perimeters 1
  ${MINI_DIMST1} ${MINI_SCHED1} --infill linesh --infill-medialaxis-radius 0.5 --additional-perimeters 2
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${MINISTL} mini_3d_infilling_withsurface
  ${MINILABELS} ${SCHED}
  ${MINI_DIMST0} ${MINI_SCHED0} --infill linesvh --infill-medialaxis-radius 0.5
  ${MINI_DIMST1} ${MINI_SCHED1} --infill linesh --infill-static-mode --infill-lineoverlap -4 --surface-infill linesh --compute-surfaces-just-with-same-process false
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${MINISALIENTSTL} mini_3d_clearance_vcorrection
  ${MINILABELS} ${SCHED} --vertical-correction
  ${MINI_DIMST0} ${MINI_SCHED0} ${CLRNCE} --infill linesh --infill-medialaxis-radius 0.5
  ${MINI_DIMST1} ${MINI_SCHED1} ${CLRNCE} --infill linesv --infill-medialaxis-radius 0.5
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${MINISTL} mini_3d_clearance_infillingrec
  ${MINILABELS} ${SCHED}
  ${MINI_DIMST0} ${MINI_SCHED0} ${CLRNCE} --infill linesh --infill-medialaxis-radius 0.5 --infilling-recursive
  ${MINI_DIMST1} ${MINI_SCHED1} ${CLRNCE} --infill linesv --infill-medialaxis-radius 0.5
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${MINISTL} mini_3d_addsub
  ${MINILABELS} ${SCHED} --addsub
  ${MINI_DIMST0} ${MINI_SCHED0} --medialaxis-radius 0.1 0.05
  ${MINI_DIMST1} ${MINI_SCHED1} --medialaxis-radius 0.5
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${MINISTL} mini_3d_infilling_addsub
  ${MINILABELS} ${SCHED} --addsub
  ${MINI_DIMST0} ${MINI_SCHED0} --medialaxis-radius 0.1 0.05 --infill linesh
  ${MINI_DIMST1} ${MINI_SCHED1} --medialaxis-radius 0.5 --infill linesh 
  )



###########################################################
###### FULL-FLEDGED TEST CASES
###########################################################

set(FULLLABELS execfull compfull)
set(NOSCHED --slicing-uniform 0.05 --save-contours --motion-planner)
set(FULL_DIMST0 --process 0 --radx 75 --tolerances 0.75 0.01 --smoothing 0.1)
set(FULL_DIMST1 --process 1 --radx 10 --tolerances 0.1  0.01 --smoothing 0.1)
set(FULL_SCHED0 --voxel-profile ellipsoid --voxel-z 75 67.5)
set(FULL_SCHED1 --voxel-profile ellipsoid --voxel-z 10 9)

#TEST_MULTIRES_BOTHSNAP is not used when computing times for *_nosnap are unreasonably large or we are already testing other things such as raw save/load.
#It is not used when it does not make sense, either (just-save-raw, DXF tests, ...).

set(ARGS_FOR_TESTRAW ${FULLSTL} full_no3d_clearance_noinfilling
  ${FULLLABELS} ${NOSCHED} ${SNAPTHIN}
  ${FULL_DIMST0} ${CLRNCE}
  ${FULL_DIMST1} ${CLRNCE}
  )
TEST_MULTIRES_COMPARE(        ${ARGS_FOR_TESTRAW})
TEST_MULTIRES_COMPARE_TESTRAW(${ARGS_FOR_TESTRAW})
TEST_MULTIRES_COMPARE(${FULLSTL} full_no3d_substractive_box
  ${FULLLABELS} ${NOSCHED} ${SNAPTHICK} --subtractive-box-mode 6000.0 5000.0
  ${FULL_DIMST0} ${FULL_SCHED0}
  ${FULL_DIMST1} ${FULL_SCHED1}
  )
TEST_MULTIRES_COMPARE(${FULLSTL} full_no3d_clearance_infillingconcentric
  ${FULLLABELS} ${NOSCHED} ${SNAPTHIN}
  ${FULL_DIMST0} ${CLRNCE} --infill concentric --infill-medialaxis-radius 0.5
  ${FULL_DIMST1} ${CLRNCE} --infill concentric --infill-medialaxis-radius 0.5
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHIN ${FULLSTL} full_no3d_clearance_infillinglines
  ${FULLLABELS} ${NOSCHED} 
  ${FULL_DIMST0} ${CLRNCE} --infill linesh --infill-medialaxis-radius 0.5
  ${FULL_DIMST1} ${CLRNCE} --infill linesv --infill-medialaxis-radius 0.5
  )
TEST_MULTIRES_COMPARE(${FULLSTL} full_no3d_clearance_infillingrecursive
  ${FULLLABELS} ${NOSCHED} ${SNAPTHIN}
  ${FULL_DIMST0} ${CLRNCE} --infill concentric --infill-medialaxis-radius 0.5 --infilling-recursive
  ${FULL_DIMST1} ${CLRNCE} --infill linesh     --infill-medialaxis-radius 0.5
  )
TEST_MULTIRES_COMPARE(${FULLSTL} full_no3d_clearance_infillingrecursive_removeredundant
  ${FULLLABELS} ${NOSCHED} ${SNAPTHIN}
  ${FULL_DIMST0} ${CLRNCE} --infill concentric --infill-medialaxis-radius 0.5 --infilling-recursive
  ${FULL_DIMST1} ${CLRNCE} --infill linesh     --infill-medialaxis-radius 0.5 --radius-removecommon 0.1
  )
TEST_MULTIRES_COMPARE(${FULLSTL} full_no3d_addsub_medialaxis
  ${FULLLABELS} ${NOSCHED} --addsub ${SNAPTHICK}
  ${FULL_DIMST0} --medialaxis-radius 0.05 --infill linesh
  ${FULL_DIMST1} --medialaxis-radius 0.5  --infill linesh
  )
TEST_MULTIRES_COMPARE(${FULLDENTEDSTL} full_no3d_addsub_negclosing_nooverwriting
  ${FULLLABELS} ${NOSCHED} ${SNAPTHICK} --addsub --neg-closing 40 --overwrite-gradual 0.9 0.5 0.4 0.6 0.2 0.8 0 1
  ${FULL_DIMST0} --infill linesh
  ${FULL_DIMST1} --infill linesh --medialaxis-radius 0.5
  )
TEST_MULTIRES_COMPARE(${FULLDENTEDSTL} full_no3d_addsub_negclosing_gradual
  ${FULLLABELS} ${NOSCHED} --addsub --neg-closing 40 --overwrite-gradual 0.9 0.5 0.4 0.6 0.2 0.8 0 1 ${SNAPTHICK}
  ${FULL_DIMST0} --infill linesh
  ${FULL_DIMST1} --infill linesh --medialaxis-radius 0.5
  )
TEST_MULTIRES_COMPARE(${FULLSTL} full_dxf_justcontours
  ${FULLLABELS} ${NOSCHED} ${USEGRID}
  --dxf-contours "${TEST_DIR}/full_dxf_justcontours" --dxf-format binary --dxf-by-tool #--dxf-by-z 
  ${FULL_DIMST0} 
  ${FULL_DIMST1} --no-toolpaths --no-preprocessing 2
  )
TEST_COMPARE(COMPARE_DXF_full_dxf_justcontours_N0 compfull full_dxf_justcontours
      "${TEST_DIR}/full_dxf_justcontours.N0.dxf"
  "${TESTPREV_DIR}/full_dxf_justcontours.N0.dxf"
  )
TEST_COMPARE(COMPARE_DXF_full_dxf_justcontours_N1 compfull full_dxf_justcontours
      "${TEST_DIR}/full_dxf_justcontours.N1.dxf"
  "${TESTPREV_DIR}/full_dxf_justcontours.N1.dxf"
  )
#we are using this as a basis for test --slicing-manual, which has to be defined slightly differently, so we cannot include some of the arguments
set(ARGS_FOR_MULTIPLE_TESTS
  ${FULLLABELS} ${SNAPTHIN}
  ${FULL_DIMST0} ${FULL_SCHED0} ${CLRNCE}
  ${FULL_DIMST1} ${FULL_SCHED1} ${CLRNCE}
  )
TEST_MULTIRES_COMPARE(          ${FULLSTL} full_3d_clearance_noinfilling ${ARGS_FOR_MULTIPLE_TESTS} ${SCHED})
TEST_MULTIRES_COMPARE_TESTRAW(  ${FULLSTL} full_3d_clearance_noinfilling ${ARGS_FOR_MULTIPLE_TESTS} ${SCHED})
TEST_MULTIRES_TESTCHECKPOINTING(${FULLSTL} full_3d_clearance_noinfilling ${ARGS_FOR_MULTIPLE_TESTS} ${SCHED})
TEST_MULTIRES_COMPARE(          ${FULLSTL} full_3d_slicing_manual        ${ARGS_FOR_MULTIPLE_TESTS}
  --save-contours --motion-planner --slicing-manual
    #these slice heights should be computed the same by --slicing-scheduler across different architectures and floating point settings
    0 -0.0028361406922340392026
    1 -0.061336140692234038252
    1 -0.043336140692234043081
    1 -0.025336140692234040972
    1 -0.0073361406922340397299
    1 0.010663859307765961512
    1 0.028663859307765961887
    1 0.046663859307765960527
    1 0.064663859307765955697
  )
#make sure that --slicing-manual produces the same results as --slicing-scheduler if all other parameters remain the same
TEST_COMPARE(full_3d_slicing_manual_compare execfull full_3d_clearance_noinfilling
      "${TEST_DIR}/full_3d_clearance_noinfilling.paths"
  "${TEST_DIR}/full_3d_slicing_manual.paths"
  )
TEST_MULTIRES_COMPARE(${FULLSTL} full_3d_clearance_infillinglines
  ${FULLLABELS} ${SCHED} ${SNAPTHIN}
  ${FULL_DIMST0} ${FULL_SCHED0} ${CLRNCE} --infill linesh --infill-medialaxis-radius 0.5
  ${FULL_DIMST1} ${FULL_SCHED1} ${CLRNCE} --infill linesv --infill-medialaxis-radius 0.5
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHICK ${FULLSTL} full_3d_clearance_infillinglines_addperimeters
  ${FULLLABELS} ${SCHED}
  ${FULL_DIMST0} ${FULL_SCHED0} --infill linesv --infill-medialaxis-radius 0.5 --additional-perimeters 1
  ${FULL_DIMST1} ${FULL_SCHED1} --infill linesh --infill-medialaxis-radius 0.5 --additional-perimeters 2
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHICK ${FULLSTL} full_3d_clearance_withsurface_infillingcrisscrosslines
  ${FULLLABELS} ${SCHED}
  ${FULL_DIMST0} ${FULL_SCHED0} --infill linesvh --infill-medialaxis-radius 0.5
  ${FULL_DIMST1} ${FULL_SCHED1} --infill linesh --infill-static-mode --infill-lineoverlap -4 --surface-infill linesh --compute-surfaces-just-with-same-process false
  )
#because the argument list is only ALMOST compatible with --load-multi, the first arguments are different for each case
set(ARGS_FOR_TESTLOADMULTI full_3d_clearance_vcorrection_infillinglines
  ${FULLLABELS} ${SCHED} --vertical-correction ${SNAPTHIN}
  ${FULL_DIMST0} ${FULL_SCHED0} ${CLRNCE} --infill linesh --infill-medialaxis-radius 0.5
  ${FULL_DIMST1} ${FULL_SCHED1} ${CLRNCE} --infill linesh --infill-medialaxis-radius 0.5
  )
TEST_MULTIRES_COMPARE(              ${FULLSALIENTSTL}        ${ARGS_FOR_TESTLOADMULTI})
TEST_MULTIRES_COMPARE_TESTLOADMULTI(${FULLSTL} ${SALIENTSTL} ${ARGS_FOR_TESTLOADMULTI})
TEST_MULTIRES_COMPARE(${FULLSTL} full_3d_clearance_infillingrecursive
  ${FULLLABELS} ${SCHED} ${SNAPTHIN}
  ${FULL_DIMST0} ${FULL_SCHED0} ${CLRNCE} --infill linesh --infilling-recursive
  ${FULL_DIMST1} ${FULL_SCHED1} ${CLRNCE} --infill linesh
  )
TEST_MULTIRES_COMPARE(${FULLSTL} full_3d_addsub_infillinglines
  ${FULLLABELS} ${SCHED} --addsub ${SNAPTHICK}
  ${FULL_DIMST0} ${FULL_SCHED0} --medialaxis-radius 0.1 0.05 --infill linesh
  ${FULL_DIMST1} ${FULL_SCHED1} --medialaxis-radius 0.5      --infill linesh
  )
TEST_MULTIRES_BOTHSNAP(SNAPTHICK ${FULLSTL} full_3d_addsub
  ${FULLLABELS} ${SCHED} --addsub
  ${FULL_DIMST0} ${FULL_SCHED0} --medialaxis-radius 0.1 0.05
  ${FULL_DIMST1} ${FULL_SCHED1} --medialaxis-radius 0.5
  )
TEST_MULTIRES_COMPARE(${FULLSUBSTL} full_3d_voxel_spec
  ${FULLLABELS} ${SCHED} --slicing-zbase 0 --slicing-direction down ${SNAPTHIN}
  --process 0 --voxel-profile interpolated --voxel-spec -36.5 50 -34 100 -31 123 -27 140 -20.5 157 -18.5 180 -13.5 206 3 256 10 285 15 300 41.5 362 --voxel-application-point 41.5 --voxel-zlimits -36.5 41.5 --smoothing 0.1
  --process 1 --voxel-profile interpolated --voxel-spec -4 4.9 0 4.9 --voxel-application-point 0 --voxel-zlimits -5 0 --smoothing 0.1 --infill linesh --infill-medialaxis-radius 1.0 0.5
  )
