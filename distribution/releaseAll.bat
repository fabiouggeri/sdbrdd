SET GOAL=%1

IF "%GOAL%"=="" SET GOAL=install

call mvn %GOAL%
call mvn %GOAL% -Darch=32
call mvn %GOAL% -Dcc=mingw64
call mvn %GOAL% -Dcc=mingw64 -Darch=32
rem CLang is binary compatible with Mingw64
rem call mvn %GOAL% -Dcc=clang
rem call mvn %GOAL% -Dcc=clang -Darch=32
call mvn %GOAL% -Dcc=bcc -Dclp=xharbour
