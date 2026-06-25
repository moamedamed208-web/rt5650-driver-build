@echo off
bcdedit /set testsigning on
if errorlevel 1 (
  echo Failed to enable testsigning. Run as Administrator.
  exit /b 1
)
echo Test signing enabled. Reboot Windows to apply.
