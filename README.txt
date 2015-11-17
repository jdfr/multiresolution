This project has been successfully compiled in MinGW-64, Visual Studio 2013, and Debian unstable (October 2015).

BEFORE COMPILING:

1. If it is not already at ../clipper, download or clone git repo at https://github.com/jdfr/clipper to have correctly placed the following files:
      ../clipper/clipper.hpp
      ../clipper/clipper.cpp
      ../clipper/iopaths/common.hpp
      ../clipper/iopaths/iopaths.hpp
      ../clipper/iopaths/iopaths.cpp

2. If it is not already at ../boost, put a copy of boost there (the originally used version is a stripped down copy of Boost 1.055). If compiling with MinGw-64, please make sure that the DLLs at ../mingwdlls match the dlls of your MinGW-64 distribution.

3. A configuration file has to be supplied. config.txt is supplied as an example. It must contain:
    *paths and options for the slicer if the code is compiled as a standalone application.
    *python and script paths if the debugging facilities at showcontours.hpp are used.

4. A parameters file is not necessary, but convenient. params.txt is supplied as an example.

TO COMPILE:

1. To compile in Visual Studio 2013:

  call mkvs.bat (requires directory ./bin_msvs to not be created yet, otherwise rmk.bat can do everything else)

2. To compile in MinGW-64:

  2.1. edit ./setpath.bat to set correctly the path to MinGW-64
  2.2. call mkgw.bat (requires directory ./bin_mingw to not be created yet, otherwise setpath.bat and rmk.bat can do everything else)

3. To compile in Linux/GCC:
   mkdir bin && cd bin && cmake .. && make
