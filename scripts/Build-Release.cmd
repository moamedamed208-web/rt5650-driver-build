@echo off
setlocal
set SCRIPT_DIR=%~dp0
set ROOT=%SCRIPT_DIR%..
msbuild "%ROOT%\Rt5650SpbFilter.sln" /p:Configuration=Release /p:Platform=x64 /t:Build
endlocal
