@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

echo ========================================
echo   Agent Framework Build Script
echo ========================================
echo.

set BUILD_DIR=build
set COMPILER=clang++

if "%1"=="clean" (
    echo Cleaning build directory...
    if exist "%BUILD_DIR%" (
        rmdir /s /q "%BUILD_DIR%"
        echo Build directory cleaned.
    ) else (
        echo Build directory does not exist.
    )
    exit /b 0
)

if "%1"=="rebuild" (
    echo Rebuilding from scratch...
    if exist "%BUILD_DIR%" (
        rmdir /s /q "%BUILD_DIR%"
    )
)

echo Step 1: Creating build directory...
if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%"
)
cd "%BUILD_DIR%"
if errorlevel 1 (
    echo Failed to create build directory.
    exit /b 1
)
echo.

echo Step 2: Configuring CMake...
echo   Compiler: %COMPILER%
cmake -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=%COMPILER% ..
if errorlevel 1 (
    echo CMake configuration failed.
    exit /b 1
)
echo.

echo Step 3: Building project...
echo   (Using parallel build with 4 threads)
cmake --build . -- -j 4
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

REM Copy openserp.exe to agent_cli directory (from project root up one level)
if exist "..\third_party\openserp\openserp.exe" (
    copy /Y "..\third_party\openserp\openserp.exe" "agent_cli\openserp.exe" >nul
    echo   Copied openserp.exe to agent_cli directory
) else (
    echo   Warning: openserp.exe not found at ..\third_party\openserp\openserp.exe
)

REM Copy obscura executables to agent_cli directory
if exist "..\third_party\obscura\obscura.exe" (
    copy /Y "..\third_party\obscura\obscura.exe" "agent_cli\obscura.exe" >nul
    echo   Copied obscura.exe to agent_cli directory
) else (
    echo   Warning: obscura.exe not found at ..\third_party\obscura\obscura.exe
)
if exist "..\third_party\obscura\obscura-worker.exe" (
    copy /Y "..\third_party\obscura\obscura-worker.exe" "agent_cli\obscura-worker.exe" >nul
    echo   Copied obscura-worker.exe to agent_cli directory
) else (
    echo   Warning: obscura-worker.exe not found at ..\third_party\obscura\obscura-worker.exe
)

REM Copy config directory to agent_cli build directory
if exist "..\agent_cli\config" (
    xcopy /E /I /Y "..\agent_cli\config" "agent_cli\config\" >nul
    echo   Copied config directory to agent_cli build directory
) else (
    echo   Warning: config directory not found at ..\agent_cli\config
)

echo.

echo ========================================
echo   Build Successful!
echo ========================================
echo.
echo Build artifacts:
echo   Static library:   build\agent_lib\src\libagent_framework.a
echo   Agent CLI:       build\agent_cli\agent_cli.exe
echo   Framework test:  build\tests\framework_test.exe
echo   Tool test:       build\tests\tool_test.exe
echo.
echo To run agent:
echo   set LLM_API_KEY=your-api-key
echo   set OPENSERP_SEARCH_ENGINES=bing
echo   .\build\agent_cli\agent_cli.exe
echo.

cd ..
