@echo off
rem Builds the casino and its tests with MinGW g++ (C++17).
rem   build.bat        build build\casino.exe and build\casino_tests.exe
rem   build.bat test   build, then run the tests
setlocal enabledelayedexpansion
if not exist build mkdir build

set FILES=
set LIBFILES=
for %%f in (src\*.cpp) do (
  set FILES=!FILES! %%f
  if /I not "%%~nxf"=="main.cpp" set LIBFILES=!LIBFILES! %%f
)
set FLAGS=-std=c++17 -O2 -Wall -Wextra -Iinclude
set LIBS=-lws2_32 -static -pthread

echo Building casino.exe ...
g++ %FLAGS% %FILES% -o build\casino.exe %LIBS% || goto :failed
echo Building casino_tests.exe ...
g++ %FLAGS% %LIBFILES% tests\tests.cpp -o build\casino_tests.exe %LIBS% || goto :failed

echo.
echo   Terminal game : build\casino.exe
echo   Web app       : build\casino.exe --web   then open http://localhost:8080/
echo   Tests         : build\casino_tests.exe
if /I "%1"=="test" build\casino_tests.exe
exit /b %errorlevel%

:failed
echo Build failed.
exit /b 1
