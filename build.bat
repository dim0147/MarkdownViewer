@echo off
setlocal
cd /d "%~dp0"

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo Could not find Visual Studio build tools. Edit the vcvars64.bat path in this script.
    exit /b 1
)

if not exist third_party\webview2\build\native\include\WebView2.h (
    echo Fetching WebView2 SDK...
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\get_webview2.ps1 || exit /b 1
)

if not exist res\icon.ico (
    echo Generating icon...
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_icon.ps1 || exit /b 1
)

echo Compiling resources...
rc /nologo /fo res\app.res res\app.rc || exit /b 1

echo Compiling...
cl /nologo /utf-8 /EHsc /O2 /W3 /std:c++17 /DUNICODE /D_UNICODE ^
   /I third_party\webview2\build\native\include ^
   src\main.cpp res\app.res /Fe:MarkdownViewer.exe /Fo:src\ ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO ^
   /LIBPATH:third_party\webview2\build\native\x64 || exit /b 1

echo.
echo Build OK: MarkdownViewer.exe
