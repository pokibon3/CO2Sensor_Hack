@echo off
rem Wrapper for tools\web_flasher.py; runs from any directory. Options are passed through.
rem Builds the games, serves tools\web\ on http://localhost:8737 and opens it in the browser (use Chrome or Edge).
rem Uses the Python launcher (py -3) if installed, otherwise python.
setlocal
set "PY=python"
where py >nul 2>nul && set "PY=py -3"
%PY% "%~dp0..\web_flasher.py" %*
exit /b %ERRORLEVEL%
