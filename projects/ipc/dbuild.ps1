$env:BK_SOLUTION_MODE = 1
$env:SOLUTION_DIR = [System.IO.Path]::GetFullPath("..\..")

function Check-SdkDir {
    param (
        [string]$SdkDir
    )
    
    $dbuildPath = Join-Path -Path $SdkDir -ChildPath "tools\build_tools\docker_build\dbuild.sh"
    
    if (-not (Test-Path $dbuildPath)) {
        Write-Host "sdk directory not found: $SdkDir, please set SDK_DIR environment variable" -ForegroundColor Red
        exit 1
    }
}

function Main {
    if ([string]::IsNullOrEmpty($env:SDK_DIR)) {
        $env:SDK_DIR = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
    }
    
    $currentDir = (Get-Location).Path
    $currentDir = [System.IO.Path]::GetFullPath($currentDir)
    
    $projectDir = $currentDir.Replace($solutionDir + "\", "").Replace("\", "/")
    $env:PROJECT_DIR = $projectDir
    $dockerBuildScript = Join-Path -Path $env:SDK_DIR -ChildPath "dbuild.ps1"
    
    Check-SdkDir -SdkDir $env:SDK_DIR
    
    & $dockerBuildScript "$args"
}

Main "$args"

