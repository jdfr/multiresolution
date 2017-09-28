#options to control build of each target
option(MAKEMR_LIBRARY       "make shared library"  ON)
option(MAKEMR_EXECUTABLE    "make standalone executable"  ON)
option(MAKEMR_CS_INTERFACE  "make the C# client of the DLL shared library (only in Visual Studio)" ${MAKEMR_LIBRARY})
option(MAKEMR_CS_AUTOCAD    "make a .NET AutoCAD plugin that uses the C# client (only in Visual Studio, requires AutoCAD's ObjectARX to be installed, and the paths to AutoCAD to be defined)" ${MAKEMR_CS_INTERFACE})
option(MAKEMR_SVGCONVERTER  "make svg converter executable"  ON)
option(MAKEMR_DXFCONVERTER  "make dxf converter executable"  ON)
option(MAKEMR_NANOCONVERTER "make nanoscribe converter executable"  ON)
option(MAKEMR_XYZHANDLER    "make utility to process xyz point cloud files"  ON)
option(MAKEMR_TRANSFORMER   "make executable to apply transformations to paths"  ON)
option(MAKEMR_FILEFILTER    "make filter for paths files"  ON)
option(MAKEMR_FILEFILTERZ   "make filterz for paths files"  ON)
option(MAKEMR_FILESPLITTER  "make a grid splitter for paths files"  ON)
option(MAKEMR_FILEINFO      "make info dumper for paths files"  ON)
option(MAKEMR_FILEUNION     "make tool to merge several paths files into one"  ON)
option(MAKEMR_FILETOUCH     "make slice header setter for paths files"  ON)

#AutoCAD configuration
set(AUTOCAD_PATH_PREFIX        "" CACHE PATH "path to AutoCAD libraries and executables (accoremgd.dll et al)")
set(DOTNET_VERSION             "v4.5" CACHE STRING "version of .NET to compile the c# interfaces and plugins (use the one suitable for your AutoCAD version. For example, 2013 uses v4.0, while 2016 uses v4.5)")

#test options
option(GENERATE_TESTS       "generate tests and test targets (IMPORTANT: DO NOT SIMPLY RUN ALL TESTS, USE THE check* TARGETS!!!!)" ON)
option(USE_GCOV             "compile with gcov support, useful for analyzing test code coverage with lcov (automatically added when executing check* targets, output on subdirectory lcov_report in the subproject's build directory)" OFF)
option(AUTOCAD_USECONSOLE   "if true, the AutoCAD tests use ACCoreConsole.exe; if false, acad.exe. The former is strongly preferred, but I was not able to make it work with AutoCAD 2013, but it worked with acad.exe. Conversely, in the 2016 version, acad.exe was problematic while AcCoreConsole.exe ran like a charm." ON)

#clipper configuration
option(CLIPPER_USE_ARENA       "enable ArenaMemoryManager for ClipperLib" ON)
set(CLIPPER_BASE_DIR           "" CACHE PATH "tweaked clipper/iopaths base dir")
set(INITIAL_ARENA_SIZE  "52428800" CACHE STRING "Initial arena size for ArenaMemoryManager")
set(BIGCHUNK_ARENA_SIZE "2097152"  CACHE STRING "Bigchunk size for ArenaMemoryManager") #1-5 MB: 1048576 2097152 3145728 4194304 5242880

#boost configuration
set(BOOST_ROOT_PATH            "" CACHE PATH "path prefix to boost library")
option(Boost_USE_STATIC_LIBS   "Use the static or the dynamic version of the Boost libraries" OFF)
option(COMPILE_PROGRAMOPTIONS  "if boost sources are in BOOST_ROOT_PATH, compile local version of boost::program_options (useful to avoid building boost twice if Slic3r is built with mingw and multiresolution with MSVS)" ON)

#python configuration
set(PYTHON_EXECUTABLE          "" CACHE PATH "vanilla python executable")
option(USE_PYTHON_VIEWER       "add support for viewing contours in python, for debugging and visualizing results" ON)

#config files configuration
option(GENERATE_CONFIGURE_FILE "generate example configuration files (only used if MAKEMR_LIBRARY or MAKEMR_EXECUTABLE are ON)" ON)
set(SHOW_RESULT_PARAMETERS     "--show 2d" CACHE PATH "configuration option for the example param.txt file")
set(INITIAL_MESH_FILE          "PUT_HERE_YOUR_STL_FILE" CACHE PATH "configuration option for the example param.txt file")

#misc configuration
option(COMPILE_OPTIMIZATIONS   "optimizations that may make the build process significantly slower" ON)
option(ENABLE_SLICER_LOGGING   "enable slicer debug output to file" ON) #this option has to be coordinated with the one used in the slicer!!!!
