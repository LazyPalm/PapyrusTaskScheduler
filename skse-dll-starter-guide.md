# SKSE DLL Starter Guide

Deliverable: save the content below as `C:\code\skse_test\SKSE-PLUGIN-SETUP.md` (plan mode blocks repo writes; implementation step is a single file copy). Content distilled from the working Spark project.

---

# Starting a New SKSE DLL Plugin (Windows)

Reference guide distilled from the Spark project (`C:\code\skse_test`). Give this document to an agent to bootstrap a new SKSE plugin without re-researching the setup.

## 0. Prerequisites (already installed on this machine)

| Tool | Location / activation |
|---|---|
| Visual Studio 2022 Build Tools (MSVC) | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat` |
| CMake | `C:\Program Files\CMake\bin` |
| Ninja | on PATH via `%LOCALAPPDATA%\Microsoft\WinGet\Packages` |
| vcpkg | `C:\vcpkg` (set `VCPKG_ROOT=C:\vcpkg`) |
| SKSE Script Extender | must be installed in the Skyrim SE folder |
| Skyrim SE | set `SKYRIM_FOLDER` (plain install) or `SKYRIM_MODS_FOLDER` (MO2/Vortex) for auto-deploy |

## 1. Project layout (minimum viable)

```
<project>/
  CMakeLists.txt          # project name == DLL name
  CMakePresets.json       # Ninja + x64 + vcpkg toolchain
  vcpkg.json              # manifest: commonlibsse-ng (+ extras)
  vcpkg-configuration.json# registries + baselines (REQUIRED for commonlibsse-ng)
  PCH.h                   # REQUIRED by add_commonlibsse_plugin
  plugin.cpp              # SKSEPluginLoad entry point
  build.cmd               # one-shot build wrapper
  .gitignore              # ignore build/, *.dll, *.pdb
  cmake/                  # empty dir ok (VCPKG_OVERLAY_TRIPLETS points at it)
```

## 2. Config files

### vcpkg.json
```json
{
    "dependencies": [
        "commonlibsse-ng",
        { "name": "cpp-httplib", "default-features": false },
        "nlohmann-json"
    ]
}
```
Only `commonlibsse-ng` is mandatory; add other libs as needed.

### vcpkg-configuration.json
`commonlibsse-ng` is NOT in the default vcpkg registry. Use **marthofdoom's fork registry**, not the old colorglass one: the colorglass package pins CommonLibSSE-NG 3.6.0, which has no relocations for Skyrim **1.7.104** and fails at load with a relocation error (this bit us on PalmGear and TaskScheduler). The fork builds the MIT-line 3.7.0 (`marthofdoom/CommonLibSSE-NG`) with the 1.7.104 id table built in — nothing extra to install for that runtime. Copy as-is:
```json
{
    "default-registry": {
        "kind": "git",
        "repository": "https://github.com/microsoft/vcpkg.git",
        "baseline": "cc288af760054fa489574bd8e22d05aa8fa01e5c"
    },
    "registries": [
        {
            "kind": "git",
            "repository": "https://github.com/marthofdoom/vcpkg-registry",
            "baseline": "9ad385261408b68f6a4b760c8ab3c3b9ac1452c6",
            "packages": ["commonlibsse-ng"]
        }
    ]
}
```
This pins `commonlibsse-ng` 3.7.0, port-version 23 (fork commit `add17c090cfa312d370bfcdd55d2eb989d086fec`). To take a newer port later, use the newest commit of the registry's `main` as `baseline`. Verified supported game versions: 1.5.97.0, 1.6.1170.0 (Address Library), 1.7.104.0 Steam (fork's built-in id table).

### CMakePresets.json
```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "base",
            "hidden": true,
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/${presetName}",
            "installDir": "${sourceDir}/install/${presetName}",
            "architecture": { "value": "x64", "strategy": "external" },
            "cacheVariables": {
                "CMAKE_CXX_COMPILER": "cl.exe",
                "CMAKE_CXX_FLAGS": "/permissive- /Zc:preprocessor /EHsc /MP /W4 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE",
                "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
                "VCPKG_TARGET_TRIPLET": "x64-windows-static-md",
                "VCPKG_OVERLAY_TRIPLETS": "${sourceDir}/cmake",
                "CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL",
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
            }
        },
        { "name": "debug", "inherits": ["base"], "displayName": "Debug",
          "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" } },
        { "name": "release", "inherits": ["base"], "displayName": "Release",
          "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } }
    ]
}
```

### CMakeLists.txt (core parts)
```cmake
cmake_minimum_required(VERSION 3.21)
project(MyPlugin VERSION 0.0.1 LANGUAGES CXX)   # project name == DLL file name

# Auto-deploy targets (from env vars; both optional)
if(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data")
endif()
if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()

find_package(CommonLibSSE CONFIG REQUIRED)
add_commonlibsse_plugin(${PROJECT_NAME} SOURCES plugin.cpp)  # list all .cpp here
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE PCH.h)     # PCH.h is REQUIRED

# POST_BUILD: copy dll (and .pdb on Debug) into ${OUTPUT_FOLDER}/SKSE/Plugins
```
Add `find_package(... CONFIG REQUIRED)` + `target_link_libraries` for extras
(e.g. `httplib::httplib`, `nlohmann_json::nlohmann_json`).

### PCH.h (required, exact content is enough)
```cpp
#pragma once
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
using namespace std::literals;
```

### build.cmd
```bat
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set VCPKG_ROOT=C:\vcpkg
set PATH=%PATH%;C:\Program Files\CMake\bin;%LOCALAPPDATA%\Microsoft\WinGet\Packages
cmake --preset release
if errorlevel 1 exit /b 1
cmake --build build/release --config Release
```
First configure takes several minutes (vcpkg fetches/builds CommonLibSSE-NG).

## 3. Minimum plugin.cpp

```cpp
#include <windows.h>

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
    SKSE::Init(skse);

    // CRITICAL: SKSE allows only ONE sender-less listener per plugin handle
    // (RegisterListener dedupes by handle, second call is silently skipped).
    // Put ALL messaging callbacks in this single listener.
    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message *msg) {
        switch (msg->type) {
            case SKSE::MessagingInterface::kPostLoad:
                // safe: init logging/config; RE:: singletons NOT fully ready
                break;
            case SKSE::MessagingInterface::kDataLoaded:
                // safe: form types loaded, print a build marker for verification
                RE::ConsoleLog::GetSingleton()->Print("Hello from MyPlugin");
                break;
        }
    });
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
        // stop worker threads, join them, flush logs HERE
    }
    return TRUE;
}
```

## 4. Build & verify loop

1. `build.cmd` (or `cmake --preset release` then `cmake --build build/release --config Release`).
2. DLL auto-copies to `<mods folder>/<Project>/SKSE/Plugins/` or `%SKYRIM_FOLDER%\Data\SKSE\Plugins\`.
3. Launch Skyrim **through SKSE**. Verify the console print / log file.
4. If the plugin silently doesn't load: check SKSE's `Data\SKSE\skse_st.log` / plugin loader log; a missing dependency DLL or wrong API version is the usual cause.

## 5. Runtime conventions (proven in Spark)

- **Config files**: resolve the plugin's own directory with `GetModuleFileNameW` + `parent_path()`; put `MyPlugin.json` next to the DLL.
- **Logging**: write a log next to the DLL, plus a marker file at `C:\Users\Public\MyPlugin.log` — survives when the game crashes before normal logs flush and is easy to find regardless of mod manager.
- **Version info**: SKSE checks the DLL's version resource; `add_commonlibsse_plugin` handles this.
- **Optional ESP**: a masterless ESP (quest with "Start Game Enabled" + topics) can be loaded alongside; ship it in the same mod folder.

## 6. Known pitfalls (hard-won)

- **One messaging listener per plugin** — see comment above; design a single dispatcher.
- **Never run a self-re-posting per-frame SKSE task on the game thread** when PrismaUI is installed — it deadlocks PrismaUI's WndProc hook install (freeze at main menu). Poll hotkeys (`GetAsyncKeyState`) on a worker thread instead.
- **PrismaUI views**: do not auto-focus an input in the view's script on load — Ultralight grabbing native focus triggers the same WndProc hook deadlock.
- **`DialogueManager` is not wrapped in CommonLibSSE-NG 3.6.0** and has no verified relocation; call Papyrus methods (e.g. `Actor.Say`) via `RE::IVirtualMachine::DispatchMethodCall` instead.
- **Repeated dialogue lines get engine-cached**: alternate between two topics when re-displaying AI text, or use `SubtitleManager::forceDisplay`.
- **Blocking work**: never block the game thread (HTTP, file IO) — use worker threads with a shutdown/drain path in `DLL_PROCESS_DETACH`.
- **Debug builds** should copy the `.pdb` next to the DLL for readable crash logs.

## 7. Quick checklist for a brand-new plugin

1. [ ] Copy the scaffold files from section 1 (rename project in CMakeLists).
2. [ ] Trim `vcpkg.json` deps to what's needed; keep `vcpkg-configuration.json` verbatim.
3. [ ] Write minimal `plugin.cpp` (section 3) with a build-marker console print.
4. [ ] `build.cmd` — first run builds vcpkg deps (slow), later runs are fast.
5. [ ] Launch via SKSE, confirm marker print + log file.
6. [ ] Only then add features (config, UI, HTTP, ESP) one at a time, rebuilding each step.
