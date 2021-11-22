@echo off

if ["%~1"]==[""] (
  @echo invalid arguments, please select configuration
  goto end
)

set "CONFIGURATION=%1"
set "SOLUTION_DIR=%~dp0..\..\solutions\texpacker_msvc16_%CONFIGURATION%"

@pushd ..
@mkdir %SOLUTION_DIR%
@pushd %SOLUTION_DIR%

CMake -G "Visual Studio 16 2019" -A Win32 "%CD%\..\..\cmake\solution_win32" -DCMAKE_BUILD_TYPE:STRING=%CONFIGURATION% -DCMAKE_CONFIGURATION_TYPES:STRING=%CONFIGURATION%
@popd
@popd

:end
@echo Done

@pause