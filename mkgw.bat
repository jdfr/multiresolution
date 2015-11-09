mkdir bin_mingw
copy rmk-mingw.bat bin_mingw\rmk.bat
copy setpath.bat bin_mingw\setpath.bat
cd bin_mingw
call setpath.bat
call rmk.bat
