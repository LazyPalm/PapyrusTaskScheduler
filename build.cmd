@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set VCPKG_ROOT=C:\vcpkg
set PATH=%PATH%;C:\Program Files\CMake\bin;%LOCALAPPDATA%\Microsoft\WinGet\Packages
rem Every build deploys DLL + scripts to C:\skyrimmods\mo2\mods\PapyrusTaskScheduler (see CMakeLists DEPLOY_FOLDER)
cmake --preset release
if errorlevel 1 exit /b 1
cmake --build build/release --config Release
