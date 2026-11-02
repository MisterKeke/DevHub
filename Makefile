# GNU Make wrapper for the existing Windows CMake/PowerShell workflows.
# On Windows, GNU Make may be named mingw32-make instead of make.
SHELL := cmd.exe
.SHELLFLAGS := /D /C
.DEFAULT_GOAL := help
.NOTPARALLEL:

# Values travel through the environment so filenames and paths are not shell code.
export ZIP_NAME QT_ROOT_DIR COMPILER_BIN CMAKE_PATH JOBS
export VCPKG_ROOT VCPKG_INSTALLED_DIR INSTALL_DEPS TEST_FILTER

.PHONY: help portable test
help:
	@powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "tools/make.ps1" -Task help

portable:
	@powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "tools/make.ps1" -Task portable

test:
	@powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "tools/make.ps1" -Task test
