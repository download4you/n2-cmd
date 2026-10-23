@setlocal EnableExtensions EnableDelayedExpansion
@set "CMDCMDLINE="
@set "CD="
@set "persianCmdLine=!CMDCMDLINE!"

:trimLeft
@if not "!persianCmdLine:~0,1!"==" " goto trimRight
@set "persianCmdLine=!persianCmdLine:~1!"
@goto trimLeft

:trimRight
@if not "!persianCmdLine:~-1!"==" " goto match
@set "persianCmdLine=!persianCmdLine:~0,-1!"
@goto trimRight

:match
@if /i "!persianCmdLine!"=="cmd" goto interactive
@if /i "!persianCmdLine!"=="cmd.exe" goto interactive
@if /i "!persianCmdLine!"=="!SystemRoot!\System32\cmd.exe" goto interactive
@if /i "!persianCmdLine!"=="!SystemRoot!\SysWOW64\cmd.exe" goto interactive
@if /i "!persianCmdLine!"==""cmd"" goto interactive
@if /i "!persianCmdLine!"==""cmd.exe"" goto interactive
@if /i "!persianCmdLine!"==""!SystemRoot!\System32\cmd.exe"" goto interactive
@if /i "!persianCmdLine!"==""!SystemRoot!\SysWOW64\cmd.exe"" goto interactive
@goto done

:interactive
@set "APP=%~dp0..\dist\current\n2-cmd.exe"
@if not exist "%APP%" goto done
@if defined CD (
  @start "" "%APP%" --cwd "!CD!"
) else (
  @start "" "%APP%"
)
@endlocal
@exit

:done
@exit /b 0
