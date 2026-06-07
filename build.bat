@echo off
setlocal
cd /d "%~dp0"

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo Could not find Visual Studio build tools. Edit the vcvars64.bat path in this script.
    exit /b 1
)

if not exist res\icon.ico (
    echo Generating icon...
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_icon.ps1 || exit /b 1
)

echo Compiling resources...
rc /nologo /fo res\app.res res\app.rc || exit /b 1

echo Compiling...
cl /nologo /utf-8 /EHsc /O2 /W3 /DUNICODE /D_UNICODE src\main.cpp res\app.res ^
   /Fe:MarkdownViewer.exe /Fo:src\ /link /SUBSYSTEM:WINDOWS /MANIFEST:NO || exit /b 1

echo.
echo Build OK: MarkdownViewer.exe
