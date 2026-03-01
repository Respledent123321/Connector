@echo off
setlocal EnableDelayedExpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%
cd /d "%ROOT%"

set "EXE=%ROOT%\build\Release\robot_gui.exe"
if not exist "%EXE%" set EXE=%ROOT%\build\robot_gui.exe

set "HAS_PREBUILT=0"
if exist "%EXE%" set HAS_PREBUILT=1

set "NEED_BUILD=1"
if "%HAS_PREBUILT%"=="1" if not "%ROBOT_FORCE_REBUILD%"=="1" set NEED_BUILD=0
if "%HAS_PREBUILT%"=="1" if "%NEED_BUILD%"=="0" (
  for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "$exe=Get-Item '%EXE%' -ErrorAction SilentlyContinue; if(-not $exe){'1'; exit}; $paths=@('src','CMakeLists.txt','config/robot.yaml'); $files=@(); foreach($p in $paths){ if(Test-Path $p){ if((Get-Item $p).PSIsContainer){ $files += Get-ChildItem -Path $p -Recurse -File -Include *.cpp,*.h,*.hpp,*.c,*.txt,*.yaml,*.yml } else { $files += Get-Item $p } } }; $newer=$files | Where-Object { $_.LastWriteTimeUtc -gt $exe.LastWriteTimeUtc } | Select-Object -First 1; if($newer){'1'} else {'0'}"`) do (
    set "SOURCE_NEWER=%%i"
  )
  if "!SOURCE_NEWER!"=="1" (
    echo Source changes detected. Rebuilding robot_gui.exe...
    set NEED_BUILD=1
  )
)

set "NEED_INSTALL=0"

if not exist "%ROOT%\runtime\llama\llama-cli.exe" set NEED_INSTALL=1
if not exist "%ROOT%\runtime\whisper\whisper-cli.exe" set NEED_INSTALL=1
if not exist "%ROOT%\runtime\piper\piper.exe" set NEED_INSTALL=1

if not exist "%ROOT%\models\llm\model.gguf" set NEED_INSTALL=1
if not exist "%ROOT%\models\llm\model.profile.txt" set NEED_INSTALL=1
set "LLM_PROFILE=balanced"
if exist "%ROOT%\models\llm\model.profile.txt" (
  for /f "usebackq delims=" %%p in ("%ROOT%\models\llm\model.profile.txt") do set LLM_PROFILE=%%p
)
set "LLM_MIN_SIZE=1000000000"
if /i "!LLM_PROFILE!"=="fast" set LLM_MIN_SIZE=200000000
if /i "!LLM_PROFILE!"=="strong" set LLM_MIN_SIZE=3000000000
for %%I in ("%ROOT%\models\llm\model.gguf") do (
  if exist "%%~fI" (
    if %%~zI LSS !LLM_MIN_SIZE! set NEED_INSTALL=1
  )
)
if not exist "%ROOT%\models\whisper\ggml-base.en.bin" set NEED_INSTALL=1
if not exist "%ROOT%\models\piper\en_US-lessac-medium.onnx" set NEED_INSTALL=1
if not exist "%ROOT%\models\piper\en_US-lessac-medium.onnx.json" set NEED_INSTALL=1

if "%NEED_BUILD%"=="1" (
  where cmake >nul 2>&1
  if errorlevel 1 set NEED_INSTALL=1

  set "VS_FOUND="
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
      set "VS_FOUND=%%i"
    )
  )
  if "!VS_FOUND!"=="" set NEED_INSTALL=1

  set "QT6_CONFIG="
  if not "%Qt6_DIR%"=="" if exist "%Qt6_DIR%\Qt6Config.cmake" set "QT6_CONFIG=%Qt6_DIR%\Qt6Config.cmake"
  if "!QT6_CONFIG!"=="" if not "%QT_DIR%"=="" if exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" set "QT6_CONFIG=%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake"
  if "!QT6_CONFIG!"=="" (
    for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "$roots=@('C:\Qt',\"$env:USERPROFILE\Qt\",\"$env:LOCALAPPDATA\Qt\",\"$env:LOCALAPPDATA\Programs\"); $hit=Get-ChildItem -Path $roots -Recurse -Filter Qt6Config.cmake -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName; if($hit){$hit}"`) do (
      set "QT6_CONFIG=%%i"
    )
  )
  if "!QT6_CONFIG!"=="" set NEED_INSTALL=1
  if not "!QT6_CONFIG!"=="" (
    for %%i in ("!QT6_CONFIG!") do set "QT6_CMAKE_DIR=%%~dpi"
    if not exist "!QT6_CMAKE_DIR!..\Qt6Multimedia\Qt6MultimediaConfig.cmake" set NEED_INSTALL=1
  )
)

if "%NEED_INSTALL%"=="1" (
  echo Missing prerequisites/runtime/models detected. Running auto-installer...
  call "%ROOT%\install_prereqs.bat"
  if errorlevel 1 (
    echo Auto-installer failed.
    exit /b 1
  )
)

if "%NEED_BUILD%"=="1" (
  if not exist build mkdir build

  if exist "%ROOT%\build\CMakeCache.txt" del /q "%ROOT%\build\CMakeCache.txt"
  if exist "%ROOT%\build\CMakeFiles" rmdir /s /q "%ROOT%\build\CMakeFiles"

  powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\bootstrap_runtime.ps1"
  if errorlevel 1 (
    echo Bootstrap checks failed.
    exit /b 1
  )

  set "CMAKE_PREFIX_ARG="
  if not "%QT_DIR%"=="" (
    set "CMAKE_PREFIX_ARG=-DCMAKE_PREFIX_PATH=%QT_DIR%"
  )

  set "QT6_DIR_ARG="
  if not "%Qt6_DIR%"=="" (
    set "QT6_RESOLVED=%Qt6_DIR%"
    set "QT6_DIR_ARG=-DQt6_DIR=%Qt6_DIR%"
  )

  if "!QT6_DIR_ARG!"=="" if exist "%ROOT%\config\qt6_dir.txt" (
    for /f "usebackq delims=" %%i in ("%ROOT%\config\qt6_dir.txt") do (
      set "QT6_FROM_FILE=%%i"
    )
    if not "!QT6_FROM_FILE!"=="" (
      set "QT6_RESOLVED=!QT6_FROM_FILE!"
      set "QT6_DIR_ARG=-DQt6_DIR=!QT6_FROM_FILE!"
      echo Using Qt6_DIR from config file: !QT6_FROM_FILE!
    )
  )

  if "!QT6_DIR_ARG!"=="" (
    for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "$roots=@('C:\Qt',\"$env:USERPROFILE\Qt\",\"$env:LOCALAPPDATA\Qt\",\"$env:LOCALAPPDATA\Programs\"); $hit=Get-ChildItem -Path $roots -Recurse -Filter Qt6Config.cmake -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName; if($hit){ Split-Path -Parent $hit }"`) do (
      set "QT6_FOUND=%%i"
    )
    if not "!QT6_FOUND!"=="" (
      set "QT6_RESOLVED=!QT6_FOUND!"
      set "QT6_DIR_ARG=-DQt6_DIR=!QT6_FOUND!"
      echo Auto-detected Qt6_DIR at: !QT6_FOUND!
    )
  )

  echo CMake Qt args: !CMAKE_PREFIX_ARG! !QT6_DIR_ARG!
  cmake -S "%ROOT%" -B "%ROOT%\build" -A x64 !CMAKE_PREFIX_ARG! !QT6_DIR_ARG!
  if errorlevel 1 (
    echo CMake configure failed. Ensure Visual Studio Build Tools 2022 and Qt6 are installed.
    echo If Qt6 is not discovered, set either:
    echo   set QT_DIR=C:\Qt\6.x.x\msvc2022_64
    echo   set Qt6_DIR=C:\Qt\6.x.x\msvc2022_64\lib\cmake\Qt6
    exit /b 1
  )

  cmake --build "%ROOT%\build" --config Release
  if errorlevel 1 (
    echo Build failed.
    exit /b 1
  )

  set EXE=%ROOT%\build\Release\robot_gui.exe
  if not exist "%EXE%" set EXE=%ROOT%\build\robot_gui.exe

  if not exist "%EXE%" (
    echo robot_gui.exe not found after build.
    exit /b 1
  )

  if not "!QT6_RESOLVED!"=="" (
    for %%i in ("!QT6_RESOLVED!\..\..\..") do set "QT_PREFIX=%%~fi"
    if exist "!QT_PREFIX!\bin\windeployqt.exe" (
      echo Deploying Qt runtime next to robot_gui.exe...
      "!QT_PREFIX!\bin\windeployqt.exe" --release "%EXE%" >nul
    )
  )
) else (
  echo Using existing robot_gui.exe. Set ROBOT_FORCE_REBUILD=1 to force rebuild.
  if not "%Qt6_DIR%"=="" set "QT6_RESOLVED=%Qt6_DIR%"
  if "!QT6_RESOLVED!"=="" if exist "%ROOT%\config\qt6_dir.txt" (
    for /f "usebackq delims=" %%i in ("%ROOT%\config\qt6_dir.txt") do set "QT6_RESOLVED=%%i"
  )
)

if not exist "%EXE%" (
  echo robot_gui.exe is missing.
  exit /b 1
)

for %%i in ("%EXE%") do set "EXE_DIR=%%~dpi"
if exist "!EXE_DIR!plugins" set "QT_PLUGIN_PATH=!EXE_DIR!plugins"
if exist "!EXE_DIR!platforms" set "QT_QPA_PLATFORM_PLUGIN_PATH=!EXE_DIR!platforms"

if not "!QT6_RESOLVED!"=="" (
  for %%i in ("!QT6_RESOLVED!\..\..\..") do set "QT_PREFIX=%%~fi"
  if exist "!QT_PREFIX!\bin" (
    set "PATH=!QT_PREFIX!\bin;!PATH!"
    if not defined QT_PLUGIN_PATH set "QT_PLUGIN_PATH=!QT_PREFIX!\plugins"
  )
)

set "ROBOT_HOME=%ROOT%"
"%EXE%"
