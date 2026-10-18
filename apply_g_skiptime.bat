@echo off
setlocal
set "REPOROOT=%~1"
if not defined REPOROOT set "REPOROOT=%CD%"
pushd "%REPOROOT%" || (echo ERROR: cannot open repository root & exit /b 1)
if not exist "XR_3DA.sln" (
  echo ERROR: XR_3DA.sln not found. Pass the source repository root.
  popd
  exit /b 1
)
git apply --check "%~dp0g_skiptime.patch"
if errorlevel 1 (
  echo ERROR: patch does not match the current files. No changes made.
  popd
  exit /b 1
)
git apply "%~dp0g_skiptime.patch"
if errorlevel 1 (
  echo ERROR: git apply failed.
  popd
  exit /b 1
)
echo g_skiptime source patch applied. Build xrGame Release / Win32.
popd
exit /b 0
