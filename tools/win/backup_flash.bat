@echo off
rem Wrapper for tools\backup_flash.py; runs from any directory. Options are passed through.
rem Uses the Python launcher (py -3) if installed, otherwise python.
setlocal
set "PY=python"
where py >nul 2>nul && set "PY=py -3"
%PY% "%~dp0..\backup_flash.py" %*
exit /b %ERRORLEVEL%
