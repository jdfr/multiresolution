This project has been successfully compiled in MinGW-64, Visual Studio 2013, and Debian unstable (October 2015).

BEFORE COMPILING:

1. If it is not already at ../clipper, download or clone git repo at https://github.com/jdfr/clipper to have correctly placed the following files:
      ../clipper/clipper/clipper.hpp
      ../clipper/clipper/clipper.cpp
      ../clipper/clipper/allocation_schemes.hpp
      ../clipper/iopaths/common.hpp
      ../clipper/iopaths/iopaths.hpp
      ../clipper/iopaths/iopaths.cpp

2. Boost (at least 1.55) is required. The build system will look for a source-code version in ../boost, but it can be located at any other place if the following variables are set:
  -Boost_INCLUDE_DIR: path to Boost include directory
  -BOOST_PROGRAMOPTIONS_DIR: path to Boost program_options source code dir (for source code distributions of Boost, this defaults to ${Boost_INCLUDE_DIR}/libs/program_options/src )
  -Boost_LIBRARIES: name of at least the Boost shared library for program_options, if BOOST_PROGRAMOPTIONS_DIR is not set.
  
3. A configuration file has to be supplied. config.txt is supplied as an example. It must contain:
    *paths and options for the slicer if the code is compiled as a standalone application.
    *python and script paths if the debugging facilities at showcontours.hpp are used.
   config.template.txt is a template for the default config.txt file that is generated at build time.
   params.template.txt is a template for an example param.txt file.

TO COMPILE:

1. edit ./setpath.bat to set correctly the path to MinGW-64 (if the MinGW-64 installation is not system-wide)

2.a. To compile in Visual Studio 2013:

  call mkvs.bat (requires directory ./bin_msvs to not be created yet, otherwise rmk.bat can do everything else)

2.b. To compile in MinGW-64:

  call mkgw.bat (requires directory ./bin_mingw to not be created yet, otherwise setpath.bat and rmk.bat can do everything else)

2.c. To compile in Linux/GCC:
   mkdir bin && cd bin && cmake .. && make
