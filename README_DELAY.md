# Filip Delay - CLAP Delay Effect

This project contains a minimal stereo CLAP delay effect for REAPER.

## Implemented features

- CLAP plugin entry point and plugin factory
- Stereo audio input and output ports
- Real-time circular delay buffer
- Automatable parameters:
  - Bypass
  - Delay Time, 1 ms to 2000 ms
  - Feedback, 0% to 95%
  - Mix, 0% to 100%
  - Output, -24 dB to +12 dB
- Basic CLAP state save/load so REAPER can restore the plugin settings in a project

## Build

From the project root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The built plugin is:

```text
build/FilipDelay.clap
```

On Windows/CLion you can reload the CMake project and build the `FilipDelay` target.

## Install in REAPER

Copy `FilipDelay.clap` to a CLAP plugin directory and rescan plugins in REAPER.

Common Windows path:

```text
C:\Program Files\Common Files\CLAP\
```

Alternative user path:

```text
%LOCALAPPDATA%\Programs\Common\CLAP\
```

Then in REAPER:

```text
Options -> Preferences -> Plug-ins -> VST -> Clear cache/re-scan
```

Search for `Filip Delay` in the FX browser.
