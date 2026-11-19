@echo off
rem A build with the light dump (create greeneye\radiance\dump while in a world). Never ship it.
setlocal
set "RAD_DEFINES=/DRADIANCE_DEV /DGREENEYE_INPUT_TEST"
set "RAD_OUT=bin-dev\loader"
call "%~dp0build.bat"
