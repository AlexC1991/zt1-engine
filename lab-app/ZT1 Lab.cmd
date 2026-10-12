@echo off
rem ZT1 Lab: the app (lab-app/). Needs build\Release\zt1-engine.exe built first
if not exist "%~dp0node_modules\electron\dist\electron.exe" (
  echo Installing the app's packages, once...
  pushd "%~dp0"
  call npm install
  popd
)
start "" "%~dp0node_modules\electron\dist\electron.exe" "%~dp0."
