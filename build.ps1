#!/usr/bin/env pwsh

param(
    [Parameter(Position=0)]
    [ValidateSet("build", "clean", "rebuild")]
    [string]$Action = "build",

    [Parameter(Position=1)]
    [int]$Jobs = 4,

    [Parameter(Position=2)]
    [string]$Compiler = "clang++"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot

if (-not $ProjectRoot) {
    $ProjectRoot = Get-Location
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Agent Framework Build Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$BuildDir = Join-Path $ProjectRoot "build"

switch ($Action) {
    "clean" {
        Write-Host "Cleaning build directory..." -ForegroundColor Yellow
        if (Test-Path $BuildDir) {
            Remove-Item -Recurse -Force $BuildDir
            Write-Host "Build directory cleaned." -ForegroundColor Green
        } else {
            Write-Host "Build directory does not exist." -ForegroundColor Gray
        }
        exit 0
    }

    "rebuild" {
        Write-Host "Rebuilding from scratch..." -ForegroundColor Yellow
        if (Test-Path $BuildDir) {
            Remove-Item -Recurse -Force $BuildDir
        }
    }
}

Write-Host "Step 1: Creating build directory..." -ForegroundColor Yellow
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}
Push-Location $BuildDir
try {
    Write-Host "Step 2: Configuring CMake..." -ForegroundColor Yellow
    Write-Host "  Compiler: $Compiler" -ForegroundColor Gray

    $cmakeArgs = @(
        "-G", "MinGW Makefiles"
        "-DCMAKE_CXX_COMPILER=$Compiler"
        $ProjectRoot
    )

    cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed"
    }

    Write-Host ""
    Write-Host "Step 3: Building project..." -ForegroundColor Yellow
    Write-Host "  Parallel jobs: $Jobs" -ForegroundColor Gray

    $buildArgs = @(
        "--build", $BuildDir
    )
    if ($Jobs -gt 1) {
        $buildArgs += "--"
        $buildArgs += "-j$Jobs"
    }

    cmake @buildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }

    # Copy openserp.exe to agent_cli directory (from build directory up one level)
    $openserpSrc = "..\third_party\openserp\openserp.exe"
    $openserpDst = "agent_cli\openserp.exe"
    if (Test-Path $openserpSrc) {
        Copy-Item -Path $openserpSrc -Destination $openserpDst -Force
        Write-Host "  Copied openserp.exe to agent_cli directory" -ForegroundColor Gray
    } else {
        Write-Host "  Warning: openserp.exe not found at $openserpSrc" -ForegroundColor Yellow
    }

    # Copy obscura executables to agent_cli directory
    $obscuraSrc = "..\third_party\obscura"
    $obscuraFiles = @("obscura.exe", "obscura-worker.exe")
    foreach ($file in $obscuraFiles) {
        $src = Join-Path $obscuraSrc $file
        $dst = Join-Path "agent_cli" $file
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination $dst -Force
            Write-Host "  Copied $file to agent_cli directory" -ForegroundColor Gray
        } else {
            Write-Host "  Warning: $file not found at $src" -ForegroundColor Yellow
        }
    }

    # Copy config directory to agent_cli build directory
    $configSrc = Join-Path $ProjectRoot "agent_cli\config"
    $configDst = Join-Path $BuildDir "agent_cli\config"
    if (Test-Path $configSrc) {
        # 先删除旧目标，再整体拷贝，防止嵌套问题
        if (Test-Path $configDst) {
            Remove-Item -Recurse -Force $configDst
        }
        Copy-Item -Path $configSrc -Destination $configDst -Recurse
        Write-Host "  Copied config directory to agent_cli build directory" -ForegroundColor Gray
    } else {
        Write-Host "  Warning: config directory not found at $configSrc" -ForegroundColor Yellow
    }

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "  Build Successful!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Build artifacts:" -ForegroundColor Cyan
    Write-Host "  Static library:   build\agent_lib\src\libagent_framework.a" -ForegroundColor White
    Write-Host "  Agent CLI:       build\agent_cli\agent_cli.exe" -ForegroundColor White
    Write-Host "  Framework test:  build\tests\framework_test.exe" -ForegroundColor White
    Write-Host "  Tool test:       build\tests\tool_test.exe" -ForegroundColor White
    Write-Host ""
    Write-Host "To run agent:" -ForegroundColor Cyan
    Write-Host '  $env:LLM_API_KEY = "your-api-key"' -ForegroundColor Gray
    Write-Host '  $env:OPENSERP_SEARCH_ENGINES = "bing"' -ForegroundColor Gray
    Write-Host "  .\build\agent_cli\agent_cli.exe" -ForegroundColor Gray
    Write-Host ""
}
finally {
    Pop-Location
}
