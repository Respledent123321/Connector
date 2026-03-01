# Robot GUI (Local-Only)

This project is a local-first desktop robot assistant with:

- Qt6 desktop GUI (chat, tools, memory, settings)
- Local-only reasoning hook (`llama-cli`)
- Local STT hook (`whisper-cli`)
- Local TTS hook (`piper`) with Windows fallback speech
- ggwave sound-link transmit/receive
- SQLite memory persistence across app restarts

The project vendors required `ggwave` source under `third_party/ggwave` so the folder is self-contained for transfer.

## Quick start (Windows)

1. Install prerequisites:
   - CMake
   - Visual Studio Build Tools (C++ toolchain)
   - Qt 6 (Widgets, Multimedia, Sql, Network)
   - Recommended: run `install_prereqs.bat` to auto-install missing tools, runtimes, and default local models
2. Optional: set `QT_DIR` to Qt CMake path (example: `C:\Qt\6.8.0\msvc2022_64`).
3. Run:

```bat
run_robot.bat
```

`run_robot.bat` now auto-runs `install_prereqs.bat` when required dependencies/models are missing.
If `build\Release\robot_gui.exe` already exists, `run_robot.bat` reuses it by default (portable mode) and skips rebuild.
Set `ROBOT_FORCE_REBUILD=1` to force a clean rebuild.

## Diagnostics

```bat
doctor.bat
```

## Configuration

Edit `config/robot.yaml` or use the Settings tab in the GUI.

Default local paths expected:

- `runtime/llama/llama-cli.exe`
- `models/llm/model.gguf`
- `runtime/whisper/whisper-cli.exe`
- `models/whisper/ggml-base.en.bin`
- `runtime/piper/piper.exe`
- `models/piper/en_US-lessac-medium.onnx`
- `models/piper/en_US-lessac-medium.onnx.json`

LLM profile is auto-selected by hardware on install:
- `fast` (0.5B), `balanced` (3B), or `strong` (7B).
- Override before install with:
  - `set ROBOT_LLM_PROFILE=fast`
  - `set ROBOT_LLM_PROFILE=balanced`
  - `set ROBOT_LLM_PROFILE=strong`
- You can also select profile in GUI Settings and click `Install/Upgrade Local Models` (runs model-only installer).

If these are missing, the app still runs with fallback behavior.

## Commands in chat

- `/remember <text>`
- `/forget <memory_id>`
- `/research <query>`
- `/tool run <tool_name>`

## Portability notes

To move to another Windows machine, copy the whole `robot/` folder.

- Keep `config/`, `data/`, `models/`, and `runtime/` together.
- Keep `build/Release/` as well if you want zero-build startup on the target PC.
- Launch with `run_robot.bat` on the target machine.
