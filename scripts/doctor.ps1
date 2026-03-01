$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot

function Check-Path([string]$label, [string]$path) {
  if (Test-Path $path) {
    Write-Host "[OK] $label -> $path" -ForegroundColor Green
  } else {
    Write-Host "[MISS] $label -> $path" -ForegroundColor Yellow
  }
}

Write-Host 'Robot doctor report' -ForegroundColor Cyan
Write-Host "Root: $root"

if (Get-Command cmake -ErrorAction SilentlyContinue) {
  Write-Host '[OK] cmake in PATH' -ForegroundColor Green
} else {
  Write-Host '[MISS] cmake in PATH' -ForegroundColor Yellow
}

if (Get-Command cl -ErrorAction SilentlyContinue) {
  Write-Host '[OK] MSVC compiler detected (cl.exe)' -ForegroundColor Green
} else {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if ($vsPath) {
      Write-Host "[OK] Visual Studio detected -> $vsPath" -ForegroundColor Green
    } else {
      Write-Host '[WARN] Visual Studio Build Tools not detected.' -ForegroundColor Yellow
    }
  } else {
    Write-Host '[WARN] cl.exe not found in PATH. Use Developer Command Prompt or install Visual Studio Build Tools.' -ForegroundColor Yellow
  }
}

$configPath = Join-Path $root 'config/robot.yaml'
Check-Path 'Config file' $configPath

$stateDb = Join-Path $root 'data/state.db'
if (Test-Path $stateDb) {
  Write-Host "[OK] state DB exists -> $stateDb" -ForegroundColor Green
} else {
  Write-Host "[INFO] state DB will be created on first app run." -ForegroundColor Cyan
}

Check-Path 'Llama CLI' (Join-Path $root 'runtime/llama/llama-cli.exe')
Check-Path 'Whisper CLI' (Join-Path $root 'runtime/whisper/whisper-cli.exe')
Check-Path 'Piper CLI' (Join-Path $root 'runtime/piper/piper.exe')

Check-Path 'LLM model' (Join-Path $root 'models/llm/model.gguf')
Check-Path 'Whisper model' (Join-Path $root 'models/whisper/ggml-base.en.bin')
Check-Path 'Piper model' (Join-Path $root 'models/piper/en_US-lessac-medium.onnx')
Check-Path 'Piper model config' (Join-Path $root 'models/piper/en_US-lessac-medium.onnx.json')

$llmPath = Join-Path $root 'models/llm/model.gguf'
$llmProfile = Join-Path $root 'models/llm/model.profile.txt'
if (Test-Path $llmPath) {
  $sizeGB = [math]::Round(((Get-Item $llmPath).Length / 1GB), 2)
  Write-Host "[OK] LLM size -> $sizeGB GB" -ForegroundColor Green
  if (Test-Path $llmProfile) {
    $profile = (Get-Content -Raw $llmProfile).Trim()
    Write-Host "[OK] LLM profile -> $profile" -ForegroundColor Green
  }
}

$qtHints = @()
if ($env:Qt6_DIR) { $qtHints += $env:Qt6_DIR }
if ($env:QT_DIR) { $qtHints += (Join-Path $env:QT_DIR 'lib/cmake/Qt6') }

$qtFound = $null
foreach ($hint in $qtHints) {
  if (Test-Path (Join-Path $hint 'Qt6Config.cmake')) {
    $qtFound = $hint
    break
  }
}

if (-not $qtFound) {
  $roots = @('C:\Qt', "$env:USERPROFILE\Qt", "$env:LOCALAPPDATA\Qt", "$env:LOCALAPPDATA\Programs")
  $hit = Get-ChildItem -Path $roots -Recurse -Filter Qt6Config.cmake -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($hit) {
    $qtFound = Split-Path -Parent $hit.FullName
  }
}

if ($qtFound) {
  Write-Host "[OK] Qt6 found -> $qtFound" -ForegroundColor Green
  Write-Host "     Suggested env: set Qt6_DIR=$qtFound" -ForegroundColor DarkGray
  $qtMulti = Join-Path (Split-Path -Parent $qtFound) 'Qt6Multimedia/Qt6MultimediaConfig.cmake'
  if (Test-Path $qtMulti) {
    Write-Host "[OK] Qt6 Multimedia found -> $qtMulti" -ForegroundColor Green
  } else {
    Write-Host "[MISS] Qt6 Multimedia module missing (Qt6MultimediaConfig.cmake)." -ForegroundColor Yellow
  }
} else {
  Write-Host "[MISS] Qt6Config.cmake not found. Install Qt6 MSVC kit and/or set Qt6_DIR." -ForegroundColor Yellow
}

Write-Host 'Doctor finished.' -ForegroundColor Cyan
