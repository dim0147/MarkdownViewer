# Downloads the WebView2 SDK (headers + static loader lib) into
# third_party\webview2 if it isn't there yet. Called by build.bat and the
# vcxproj pre-build step. Pin the version here when upgrading.
$ErrorActionPreference = 'Stop'
$version = '1.0.3967.48'

$root = Split-Path $PSScriptRoot -Parent
$dest = Join-Path $root 'third_party\webview2'
if (Test-Path (Join-Path $dest 'build\native\include\WebView2.h')) { exit 0 }

Write-Host "Downloading WebView2 SDK $version..."
$zip = Join-Path $env:TEMP "webview2-sdk-$version.zip"
Invoke-WebRequest "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$version" -OutFile $zip
Expand-Archive $zip $dest -Force
Remove-Item $zip
Write-Host "WebView2 SDK extracted to $dest"
