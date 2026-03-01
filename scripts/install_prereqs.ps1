param(
  [switch]$Force,
  [ValidateSet('fast', 'balanced', 'strong')]
  [string]$LlmProfile,
  [switch]$ModelsOnly
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$runtimeDir = Join-Path $root 'runtime'
$modelsDir = Join-Path $root 'models'

$llamaDir = Join-Path $runtimeDir 'llama'
$whisperDir = Join-Path $runtimeDir 'whisper'
$piperDir = Join-Path $runtimeDir 'piper'

$llmModelPath = Join-Path $modelsDir 'llm/model.gguf'
$llmProfilePath = Join-Path $modelsDir 'llm/model.profile.txt'
$whisperModelPath = Join-Path $modelsDir 'whisper/ggml-base.en.bin'
$piperModelPath = Join-Path $modelsDir 'piper/en_US-lessac-medium.onnx'
$piperModelCfgPath = Join-Path $modelsDir 'piper/en_US-lessac-medium.onnx.json'

function Ensure-Dir([string]$path) {
  if (-not (Test-Path $path)) {
    New-Item -ItemType Directory -Path $path -Force | Out-Null
  }
}

function Ensure-Download([string]$url, [string]$dest) {
  if ((Test-Path $dest) -and -not $Force) {
    Write-Host "[SKIP] Exists: $dest" -ForegroundColor DarkGray
    return
  }

  Ensure-Dir (Split-Path -Parent $dest)
  Write-Host "[GET] $url" -ForegroundColor Cyan

  $tmp = "$dest.part"
  if (Test-Path $tmp) {
    Remove-Item $tmp -Force
  }

  & curl.exe -L --fail --retry 5 --retry-all-errors --connect-timeout 20 --output $tmp $url
  if ($LASTEXITCODE -ne 0) {
    throw "Download failed: $url"
  }

  Move-Item -Force $tmp $dest
}

function Get-Qt6Dir {
  if ($env:Qt6_DIR -and (Test-Path (Join-Path $env:Qt6_DIR 'Qt6Config.cmake'))) {
    return $env:Qt6_DIR
  }

  $roots = @('C:\Qt', "$env:USERPROFILE\Qt", "$env:LOCALAPPDATA\Qt", "$env:LOCALAPPDATA\Programs")
  $hit = Get-ChildItem -Path $roots -Recurse -Filter Qt6Config.cmake -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($hit) {
    return (Split-Path -Parent $hit.FullName)
  }

  return $null
}

function Ensure-CMake {
  if (Get-Command cmake -ErrorAction SilentlyContinue) {
    Write-Host '[OK] cmake already installed' -ForegroundColor Green
    return
  }

  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw 'winget is required to auto-install cmake'
  }

  Write-Host '[INSTALL] CMake via winget' -ForegroundColor Yellow
  winget install --id Kitware.CMake --exact --silent --accept-package-agreements --accept-source-agreements --disable-interactivity
}

function Ensure-BuildTools {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if ($vs) {
      Write-Host "[OK] Visual Studio Build Tools detected: $vs" -ForegroundColor Green
      return
    }
  }

  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw 'winget is required to auto-install Visual Studio Build Tools'
  }

  Write-Host '[INSTALL] Visual Studio Build Tools 2022 via winget' -ForegroundColor Yellow
  winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --silent --accept-package-agreements --accept-source-agreements --disable-interactivity
}

function Ensure-PythonPipPackage([string]$package) {
  Write-Host "[INSTALL] python package: $package" -ForegroundColor Yellow
  python -m pip install --upgrade $package
  if ($LASTEXITCODE -ne 0) {
    throw "pip install failed: $package"
  }
}

function Ensure-Qt {
  $qt6 = Get-Qt6Dir
  if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw 'Python is required to auto-install Qt via aqtinstall'
  }

  Ensure-PythonPipPackage 'aqtinstall'

  $qtVersion = '6.8.3'
  $qtTarget = 'win64_msvc2022_64'
  $qtRoot = 'C:\Qt'

  Ensure-Dir $qtRoot

  if (-not $qt6) {
    Write-Host "[INSTALL] Qt $qtVersion ($qtTarget) via aqtinstall" -ForegroundColor Yellow
    python -m aqt install-qt windows desktop $qtVersion $qtTarget -O $qtRoot
    if ($LASTEXITCODE -ne 0) {
      throw 'aqtinstall failed while installing Qt base'
    }
  } else {
    Write-Host "[OK] Qt6 found: $qt6" -ForegroundColor Green
  }

  $qt6 = Get-Qt6Dir
  if (-not $qt6) {
    throw 'Qt install finished but Qt6Config.cmake was not found'
  }

  $qtMultimediaConfig = Join-Path (Split-Path -Parent $qt6) 'Qt6Multimedia/Qt6MultimediaConfig.cmake'
  if (-not (Test-Path $qtMultimediaConfig)) {
    Write-Host '[INSTALL] Qt Multimedia module via aqtinstall' -ForegroundColor Yellow
    python -m aqt install-qt windows desktop $qtVersion $qtTarget -O $qtRoot --modules qtmultimedia
    if ($LASTEXITCODE -ne 0) {
      throw 'aqtinstall failed while installing qtmultimedia module'
    }
  }

  Write-Host "[OK] Qt6 installed: $qt6" -ForegroundColor Green
  setx Qt6_DIR "$qt6" | Out-Null
}

function Install-GitHubZipAsset([string]$apiUrl, [string]$assetNameLike, [string]$destDir) {
  Ensure-Dir $destDir

  $release = Invoke-RestMethod $apiUrl
  $asset = $release.assets | Where-Object { $_.name -like $assetNameLike } | Select-Object -First 1
  if (-not $asset) {
    throw "Could not find release asset matching '$assetNameLike' from $apiUrl"
  }

  $tmpZip = Join-Path $env:TEMP $asset.name
  Ensure-Download $asset.browser_download_url $tmpZip

  $extractDir = Join-Path $env:TEMP ("robot_extract_" + [Guid]::NewGuid().ToString('N'))
  Ensure-Dir $extractDir

  Expand-Archive -Path $tmpZip -DestinationPath $extractDir -Force

  $copied = $false
  $entries = Get-ChildItem -Path $extractDir -Recurse -File
  foreach ($f in $entries) {
    $ext = $f.Extension.ToLowerInvariant()
    if ($ext -in @('.exe', '.dll', '.json', '.txt', '.md', '.bin', '.onnx')) {
      Copy-Item -Path $f.FullName -Destination (Join-Path $destDir $f.Name) -Force
      $copied = $true
    }
  }

  if (-not $copied) {
    throw "No usable files found in downloaded archive: $($asset.name)"
  }

  Remove-Item -Path $extractDir -Recurse -Force
}

function Ensure-LlamaRuntime {
  $exe = Join-Path $llamaDir 'llama-cli.exe'
  if ((Test-Path $exe) -and -not $Force) {
    Write-Host '[OK] llama runtime already present' -ForegroundColor Green
    return
  }

  Write-Host '[INSTALL] llama.cpp runtime' -ForegroundColor Yellow
  Install-GitHubZipAsset 'https://api.github.com/repos/ggerganov/llama.cpp/releases/latest' 'llama-*-bin-win-cpu-x64.zip' $llamaDir

  if (-not (Test-Path $exe)) {
    throw 'llama-cli.exe not found after llama.cpp install'
  }
}

function Ensure-WhisperRuntime {
  $exe = Join-Path $whisperDir 'whisper-cli.exe'
  if ((Test-Path $exe) -and -not $Force) {
    Write-Host '[OK] whisper runtime already present' -ForegroundColor Green
    return
  }

  Write-Host '[INSTALL] whisper.cpp runtime' -ForegroundColor Yellow
  Install-GitHubZipAsset 'https://api.github.com/repos/ggerganov/whisper.cpp/releases/latest' 'whisper-bin-x64.zip' $whisperDir

  if (-not (Test-Path $exe)) {
    throw 'whisper-cli.exe not found after whisper.cpp install'
  }
}

function Ensure-PiperRuntime {
  $exe = Join-Path $piperDir 'piper.exe'
  if ((Test-Path $exe) -and -not $Force) {
    Write-Host '[OK] piper runtime already present' -ForegroundColor Green
    return
  }

  Write-Host '[INSTALL] piper runtime' -ForegroundColor Yellow
  Install-GitHubZipAsset 'https://api.github.com/repos/rhasspy/piper/releases/latest' 'piper_windows_amd64.zip' $piperDir

  if (-not (Test-Path $exe)) {
    throw 'piper.exe not found after piper install'
  }
}

function Ensure-Models {
  Write-Host '[INSTALL] default models (local)' -ForegroundColor Yellow

  $ramGB = 8.0
  $freeGB = 8.0
  try {
    $ramGB = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
  } catch {
    Write-Host '[WARN] Unable to query system RAM; defaulting to balanced profile heuristics.' -ForegroundColor Yellow
  }
  try {
    $drive = (Get-Item $root).PSDrive
    if ($drive -and $drive.Free -ne $null) {
      $freeGB = [math]::Round($drive.Free / 1GB, 1)
    }
  } catch {
    Write-Host '[WARN] Unable to query free disk; defaulting to balanced profile heuristics.' -ForegroundColor Yellow
  }

  $desiredProfile = 'balanced'
  $llmUrl = 'https://huggingface.co/bartowski/Qwen2.5-3B-Instruct-GGUF/resolve/main/Qwen2.5-3B-Instruct-Q4_K_M.gguf'

  if ($LlmProfile) {
    $desiredProfile = $LlmProfile.ToLowerInvariant()
  } elseif ($env:ROBOT_LLM_PROFILE) {
    $p = $env:ROBOT_LLM_PROFILE.ToLowerInvariant()
    if ($p -in @('fast', 'balanced', 'strong')) {
      $desiredProfile = $p
    }
  } elseif ($ramGB -ge 16 -and $freeGB -ge 12) {
    $desiredProfile = 'strong'
  } elseif ($ramGB -lt 8 -or $freeGB -lt 6) {
    $desiredProfile = 'fast'
  }

  if ($desiredProfile -eq 'strong') {
    $llmUrl = 'https://huggingface.co/bartowski/Qwen2.5-7B-Instruct-GGUF/resolve/main/Qwen2.5-7B-Instruct-Q4_K_M.gguf'
  } elseif ($desiredProfile -eq 'fast') {
    $llmUrl = 'https://huggingface.co/bartowski/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/Qwen2.5-0.5B-Instruct-Q4_K_M.gguf'
  }

  $minSizeGbForProfile = 0.2
  if ($desiredProfile -eq 'balanced') { $minSizeGbForProfile = 1.0 }
  if ($desiredProfile -eq 'strong') { $minSizeGbForProfile = 3.0 }

  $needsLlmDownload = $Force
  if (-not $needsLlmDownload) {
    if (-not (Test-Path $llmModelPath)) {
      $needsLlmDownload = $true
    } else {
      $currentProfile = ''
      if (Test-Path $llmProfilePath) {
        $currentProfile = (Get-Content -Raw $llmProfilePath).Trim().ToLowerInvariant()
      }
      $sizeGB = [math]::Round(((Get-Item $llmModelPath).Length / 1GB), 2)

      if ($currentProfile -ne $desiredProfile) {
        $needsLlmDownload = $true
      } elseif ($currentProfile -eq $desiredProfile) {
        $needsLlmDownload = ($sizeGB -lt $minSizeGbForProfile)
      } elseif ($currentProfile -eq '' -and $sizeGB -lt 1.0 -and $desiredProfile -ne 'fast') {
        # Legacy tiny model without profile marker: upgrade automatically.
        $needsLlmDownload = $true
      } else {
        $needsLlmDownload = $false
      }
    }
  }

  if ($needsLlmDownload) {
    Write-Host "[INSTALL] LLM profile '$desiredProfile' (RAM=${ramGB}GB, free=${freeGB}GB)" -ForegroundColor Yellow
    if (Test-Path $llmModelPath) {
      Remove-Item $llmModelPath -Force
    }
    Ensure-Download $llmUrl $llmModelPath
    Set-Content -Path $llmProfilePath -Value $desiredProfile -Encoding ASCII
  } else {
    Write-Host "[OK] LLM model already present ($llmModelPath)" -ForegroundColor Green
    if (-not (Test-Path $llmProfilePath)) {
      Set-Content -Path $llmProfilePath -Value $desiredProfile -Encoding ASCII
    }
  }

  Ensure-Download 'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin' $whisperModelPath
  Ensure-Download 'https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx' $piperModelPath
  Ensure-Download 'https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx.json' $piperModelCfgPath
}

function Main {
  Ensure-Dir $runtimeDir
  Ensure-Dir $modelsDir
  Ensure-Dir $llamaDir
  Ensure-Dir $whisperDir
  Ensure-Dir $piperDir

  if (-not $ModelsOnly) {
    Ensure-CMake
    Ensure-BuildTools
    Ensure-Qt

    Ensure-LlamaRuntime
    Ensure-WhisperRuntime
    Ensure-PiperRuntime
  }
  Ensure-Models

  Write-Host ''
  Write-Host '[DONE] Bootstrap complete.' -ForegroundColor Green
  $qtDir = Get-Qt6Dir
  if ($qtDir) {
    $qtDirFile = Join-Path $root 'config/qt6_dir.txt'
    Set-Content -Path $qtDirFile -Value $qtDir -Encoding ASCII
  }
  Write-Host "Qt6_DIR=$qtDir"
  Write-Host "Run next: run_robot.bat"
}

Main
