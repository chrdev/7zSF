::@echo off
setlocal

set cmd_vswhere="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %cmd_vswhere% (
echo Microsoft Visual Studio not installed.
set errorlevel=1
exit /b
)

for /f "delims=" %%# in ('%cmd_vswhere% -latest -property resolvedInstallationPath') do @set path_vs=%%#


call "%path_vs%\VC\Auxiliary\Build\vcvars64.bat"
if not %VSCMD_ARG_TGT_ARCH%==x64 (
echo No building environment for x64.
set errorlevel=1
exit /b
)
nmake
call :CLEAR_VSENV

call "%path_vs%\VC\Auxiliary\Build\vcvars32.bat"
if not %VSCMD_ARG_TGT_ARCH%==x86 (
echo No building environment for x86.
set errorlevel=1
exit /b
)
nmake
call :CLEAR_VSENV


set R=build\release
if not exist %R% mkdir %R%

call :UPX 64
call :UPX 86
if exist README.* copy /y README.* %R%\

set NAME7Z=7zSF
if exist %R%\%NAME7Z%.7z del /f /q %R%\%NAME7Z%.7z
pushd build
7z a -mx -mtr=off -stl release\%NAME7Z%.7z -w"%TEMP%" release
popd
7z rn "%R%\%NAME7Z%.7z" release %NAME7Z%
goto :EOF


:UPX
set U=upx --best --force-overwrite
%U% build\x%1\7zSf.exe -o%R%\7zSF%1.sfx
%U% build\x%1\7zSf_admin.exe -o%R%\7zSF%1_admin.sfx
%U% build\x%1\7zSf_con.exe -o%R%\7zSF%1_con.sfx
%U% build\x%1\7zSf_con_admin.exe -o%R%\7zSF%1_con_admin.sfx
exit /b

:CLEAR_VSENV
set EXTERNAL_INCLUDE=
set INCLUDE=
set LIB=
set LIBPATH=
set path=%__VSCMD_PREINIT_PATH%
exit /b
