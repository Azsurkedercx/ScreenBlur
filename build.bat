@echo off
setlocal

:: ============================================================
::  ScreenBlur 构建脚本
::
::  依赖：w64devkit（g++、windres）已加入系统 PATH
::  用法：双击本脚本，生成 ScreenBlur.exe
:: ============================================================

cd /d "%~dp0"

echo ============================================
echo   Building ScreenBlur.exe
echo ============================================
echo.

:: ---- 检查编译工具 ----
where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ not found in PATH.
    echo         Please install w64devkit and add its bin folder to PATH.
    pause
    exit /b 1
)

where windres >nul 2>nul
if errorlevel 1 (
    echo [ERROR] windres not found in PATH.
    pause
    exit /b 1
)

:: ---- 步骤 1：编译资源 ----
echo [1/2] Compiling resources ...
windres ScreenBlur.rc -O coff -o ScreenBlur_res.o
if errorlevel 1 (
    echo.
    echo *** RESOURCE BUILD FAILED ***
    pause
    exit /b 1
)

:: ---- 步骤 2：编译主程序 ----
echo [2/2] Compiling C++ ...
g++ -O2 -municode -mwindows -static -s ^
    -finput-charset=UTF-8 -fexec-charset=UTF-8 ^
    -o ScreenBlur.exe ScreenBlur.cpp ScreenBlur_res.o ^
    -lshell32 -luser32 -lgdi32 -lmsimg32

if errorlevel 1 (
    echo.
    echo *** BUILD FAILED ***
    pause
    exit /b 1
)

:: ---- 清理中间文件 ----
if exist ScreenBlur_res.o del ScreenBlur_res.o

echo.
echo   OK. ScreenBlur.exe created.
echo.
dir ScreenBlur.exe
pause