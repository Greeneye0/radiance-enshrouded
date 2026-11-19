@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build mkdir build
if not defined RAD_OUT set "RAD_OUT=bin\loader"
if not exist "%RAD_OUT%" mkdir "%RAD_OUT%"
cl /nologo /std:c++20 /utf-8 /O2 /MT /W4 /EHa /DUNICODE %RAD_DEFINES% /Fobuild\ /LD src\main.cpp /link /MAP:build\radiance.map /IMPLIB:build\radiance.lib /OUT:%RAD_OUT%\radiance.dll user32.lib kernel32.lib || exit /b 1
copy /y mod.json %RAD_OUT%\mod.json >nul || exit /b 1
copy /y radiance.ini %RAD_OUT%\radiance.ini >nul || exit /b 1
echo Built %RAD_OUT%\radiance.dll for Greeneye Loader API 2
