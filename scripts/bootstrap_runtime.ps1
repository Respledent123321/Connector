param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$dirs = @(
  'build',
  'runtime',
  'runtime/llama',
  'runtime/whisper',
  'runtime/piper',
  'models',
  'models/llm',
  'models/whisper',
  'models/piper',
  'data',
  'data/tools',
  'data/logs',
  'data/cache',
  'data/cache/web'
)

foreach ($d in $dirs) {
  $path = Join-Path $root $d
  if (-not (Test-Path $path)) {
    New-Item -ItemType Directory -Path $path | Out-Null
  }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  Write-Host 'ERROR: cmake not found in PATH.' -ForegroundColor Red
  exit 1
}

Write-Host 'Bootstrap checks complete.' -ForegroundColor Green
Write-Host 'Tip: set QT_DIR to your Qt6 CMake directory if CMake cannot find Qt6.'
exit 0
