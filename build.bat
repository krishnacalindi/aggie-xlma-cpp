@echo off

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/Users/krish/vcpkg/scripts/buildsystems/vcpkg.cmake
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

cmake --build build --config Release
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo.
echo Build complete.