if exist .\CMakeLists.txt (
  echo DO NOT RUN THIS SCRIPT FROM THE BASE DIRECTORY! YOU WILL MESS EVERYTHING!!!!
  goto :eof
)
cmake .. -G "Visual Studio 12 2013 Win64"
cmake --build . --config Release
