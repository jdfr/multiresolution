if exist .\CMakeLists.txt (
  echo DO NOT RUN THIS SCRIPT FROM THE BASE DIRECTORY! YOU WILL MESS EVERYTHING!!!!
  goto :eof
)
set /p PYTHON_EXECUTABLE="Enter absolute path to the Python executable (you can leave this empty if you have installed Python system-wide): "
cmake -G "MinGW Makefiles" -D PYTHON_EXECUTABLE=%PYTHON_EXECUTABLE% ..
cmake --build .
