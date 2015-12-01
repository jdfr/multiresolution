if exist .\CMakeLists.txt (
  echo DO NOT RUN THIS SCRIPT FROM THE BASE DIRECTORY! YOU WILL MESS EVERYTHING!!!!
  goto :eof
)
cmake .. -G "MinGW Makefiles"
cmake --build .
