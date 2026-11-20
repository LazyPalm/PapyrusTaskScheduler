# PapyrusTaskScheduler — Native SKSE Process Task Scheduler

## Goal
Build a generic, save-specific task scheduler as an SKSE DLL (CommonLibSSE-NG) that any mod can use: mods schedule named one-shot tasks ("fire my process in X seconds" or "in N game hours"), the scheduler stores the queue in the save's SKSE co-save (inherently per-save), ticks while the game process runs, and fires due tasks as SKSE mod-events that any mod's script can `RegisterForModEvent` to. A Task Viewer page is registered in the SKSE Menu Framework (Nexus 120352) Mod Control Panel showing all pending tasks and allowing cancellation.

## Context (verified)
- Reference implementation decompiled from `C:\skyrimmods\mo2\mods\BindingUpdated\Scripts\bindc_TaskScheduler.pex` (saved to `C:\Users\Barry\AppData\Local\Temp\kilo\binding_updated_source\bindc_TaskScheduler.psc`): Papyrus quest script, 128-slot string/float arrays, `utility.Wait(5)` dispatcher loop, fires via `modevent.Create/Send`. Its save-persistence is quest Auto properties — fragile, size-capped, VM-tied. The native DLL replaces all of that.
- Toolchain present (per `skse-dll-starter-guide.md`): VS2022 Build Tools, CMake, Ninja, vcpkg at `C:\vcpkg`, MO2 instance at `C:\skyrimmods\mo2`.
- **Game runtime is Skyrim SE 1.7.104** (Address Library ships `versionlib-1-7-104-0.bin`). The colorglass `commonlibsse-ng` 3.6.0 has no relocations for 1.7 → "relocation error on load". Fix (same as PalmGear): pull the fork **marthofdoom/CommonLibSSE-NG** (MIT 3.7.0 line with the 1.7.104 id table built in) via `marthofdoom/vcpkg-registry`. `vcpkg-configuration.json` routes `commonlibsse-ng` to that registry (baseline commit `9ad385261408b68f6a4b760c8ab3c3b9ac1452c6`, port `3.7.0#23`).
- SKSE Menu Framework is enabled in the MO2 instance (`SKSEMenuFramework.dll`). Modder API = single header `SKSEMenuFramework.h` (github.com/QTR-Modding/SKSE-Menu-Framework-3-API): `IsInstalled()`, `SetSection()`, `AddSectionItem(path, __stdcall RenderFunction)`, plus a bridged `ImGui::` namespace — no ImGui linking required.
- Game clock is available natively as vanilla globals in `Skyrim.esm`: `GameHour` (local `0x38`, GlobalFloat) and `GameDaysPassed` (local `0x39`, GlobalFloat) — total game hours = `GameDaysPassed * 24 + GameHour`. Verify engine-maintained during testing.
- Papyrus mod-event handler signature (SKSE docs): `Event Handler(string eventName, string strArg, float numArg, Form sender)`.

## Decisions (agreed with user)
1. **Architecture:** native SKSE DLL. No companion ESP required.
2. **Two scheduling modes:**
   - `AddDelayTask(name, seconds)` — countdown burned only while the game process is running (pauses when the game is closed; survives save/load with the remaining seconds stored).
   - `AddGameTimeTask(name, gameHours)` — resolved to an absolute total-game-hours deadline at schedule time; fires when the save's game clock reaches it (fires immediately on load if already passed).
3. **Game-time target form:** relative ("in N game hours").
4. **Semantics:** one-shot; re-adding the same task name overwrites its deadline; explicit `RemoveTask`.
5. **Storage:** SKSE co-save (`SerializationInterface` Save/Load/Revert callbacks, unique 4CC `TKSC`). Only strings/doubles are persisted — no FormIDs — so load-order shifts cannot corrupt the queue.
6. **Dispatch:** construct `SKSE::ModCallbackEvent { eventName = task name, strArg = task name, numArg = 0 }` and `SendEvent` on `SKSE::GetModCallbackEventSource()`; marshal onto the game thread via `SKSE::GetTaskInterface()->AddTask` (one-shot; never self-requeue).
7. **Task Viewer UI:** a "Task Scheduler" section page in the SKSE Menu Framework listing all pending tasks (name, type, remaining time) with a Cancel button per row. Register at `kDataLoaded`; no-op if the framework is not installed.

## Plugin API (class `TaskScheduler`, all `global native` in the shipped hidden script)
```
Scriptname TaskScheduler Hidden
bool AddDelayTask(string aName, float aSeconds) global native
bool AddGameTimeTask(string aName, float aGameHours) global native
bool RemoveTask(string aName) global native
bool HasTask(string aName) global native
float GetRemainingSeconds(string aName) global native      ; -1 if absent or wrong mode
float GetRemainingGameHours(string aName) global native    ; -1 if absent or wrong mode
```
Consumer mod integration contract:
```papyrus
Self.RegisterForModEvent("MyPrefix_MiningProcess", "OnTaskDue")
Event OnTaskDue(String aEventName, String aTaskName, Float aNumArg, Form aSender)
    ; run the mod's process here
EndEvent
```

## Internal design
- `struct Task { std::string name; TaskMode mode; double deadline; }` — `Delay` stores seconds remaining; `GameTime` stores absolute target game hours.
- `std::unordered_map<std::string, Task>` guarded by a mutex; cap 1024 tasks, max name 64 chars.
- Worker thread started on `kDataLoaded`: adaptive wait (1 s tick while tasks are queued, 5 s condition-variable wait when idle, waking on add/Stop); Delay deadlines burned by `steady_clock` delta; GameTime deadlines compared against current total game hours (`RE::TESDataHandler::LookupForm(56/57, "Skyrim.esm")` as `RE::TESGlobal`).
- Due tasks: erase under lock, dispatch via `AddTask` game-thread lambda.
- Co-save format v3, record `'TASK'`: `uint32 count`; per entry `uint16 len` + name bytes, `uint8 mode`, `double deadline`, `float payloadNumber`, `FormID payloadFormID` (v2+), `uint16 len` + source mod bytes, `uint16 len` + source script bytes (v3); v1/v2 records still parsed. Bounds-check before allocating; fail-soft per record.
- UI snapshot: `Scheduler::Snapshot()` returns a vector copy under lock, rendered with `ImGui::BeginTable` rows.

## Project scaffold (all in `C:\code\PapyrusTaskScheduler`)
```
CMakeLists.txt                 # project(TaskScheduler VERSION 0.1.0); add_commonlibsse_plugin
CMakePresets.json              # verbatim from guide
vcpkg.json                     # ["commonlibsse-ng"]
vcpkg-configuration.json       # marthofdoom registry for commonlibsse-ng (3.7.0#23, 1.7.104 support)
PCH.h  build.cmd  cmake/  .gitignore
third_party/SKSEMenuFramework.h  # downloaded API header
src/plugin.cpp                 # SKSEPluginLoad: log, Init, co-save registration, native fns, kDataLoaded -> Start() + UI register
src/Scheduler.h / Scheduler.cpp  # queue, tick loop, dispatch, Save/Load/Revert
src/UI.h / UI.cpp              # SKSE Menu Framework Task Viewer page
Scripts/TaskScheduler.psc      # hidden script with native declarations
Scripts/TaskScheduler.pex      # compiled via housecarl_compile_script
example/ExampleConsumer.psc    # demo: registers mod event, schedules both task types, logs on fire
```
Deploy: **every build copies the DLL and the compiled scripts to `C:\skyrimmods\mo2\mods\PapyrusTaskScheduler`** (`SKSE/Plugins/TaskScheduler.dll` + `Scripts/*.pex` + `Scripts/Source/*.psc`). This is the single deploy target, hard-coded in `CMakeLists.txt` as `DEPLOY_FOLDER` via POST_BUILD; enable the mod in MO2.

## Implementation status (2026-11-20) — v1 complete, v2 payload + source attribution + adaptive tick added, confirmed working in-game
- Built with **CommonLibSSE-NG 3.7.0#23 (marthofdoom fork)**, the fix for the 1.7.104 relocation failure; DLL deployed to `C:\skyrimmods\mo2\mods\PapyrusTaskScheduler\SKSE\Plugins\PapyrusTaskScheduler.dll`, scripts in `Scripts\` + `Scripts\Source\`, `meta.ini` present. Mod enabled in MO2; loads cleanly via SKSE.
- `TaskScheduler.psc` and `ExampleTaskConsumer.psc` compiled OK via houseCARL.
- Build fixes learned (still true on 3.7.0): native Papyrus functions must take parameters **by value** (`is_not_reference` rejects `const std::string&`); `GetSingleton()` accessors are const, so `LookupForm`/`SendEvent` need `const_cast`; entry point is `extern "C" __declspec(dllexport) bool SKSEPlugin_Load` (`SKSEPluginInfo` in the generated file supplies `SKSEPlugin_Version` + `SKSEPlugin_Query`).
- `skse-dll-starter-guide.md` updated to reference the marthofdoom registry for future projects.
- **Caller source attribution (done):** `AddDelayTask`/`AddGameTimeTask` natives use the long signature (`VirtualMachine*`, `VMStackID`) to get the exact caller stack via `GetStackByID`, walk the frame chain, record the calling script class name, and resolve the defining plugin from the bound/attached record handle (vanilla handle = FormID in the low 32 bits; ESLs resolved via `LookupLoadedLightModByIndex`). Payload-form owner is the fallback; unattached framework instances (e.g. `bindt_Main`'s global functions) have no VM record link, so the mod column stays "unknown" while the script name is still captured. Co-save format bumped to v3 (v1/v2 still readable).
- **Adaptive worker tick (done):** idle waits use a condition variable (5 s, waking immediately on task add or Stop); active tasks tick once per second. The idle period is no longer charged against a freshly added task's countdown.
- **UI (done):** Menu Framework section renamed to "Papyrus Task Scheduler"; Task Viewer table now includes "Source Mod" and "Source Script" columns.

## Remaining validation (all spot-checked in game — 2026-11-20)
1. ✅ `PapyrusTaskScheduler.log` appears in `Documents\My Games\Skyrim Special Edition\SKSE\`.
2. ✅ Mod Control Panel (F1) → "Papyrus Task Scheduler" → "Task Viewer" lists pending tasks with Source Mod / Source Script columns; Cancel removes one.
3. ✅ Tasks fire and are consumed by real mods (BindingTop's `SummonSubsTask` fired via mod event).
4. ✅ Save-specific persistence: tasks survive save/quit/reload (`loaded N task(s) from co-save`); a new game starts with an empty queue (Revert).
5. ✅ Countdown accuracy: delay tasks burn real-time seconds with a 1 s tick; no idle-period charge after the adaptive-sleep fix.

## Next steps (v2 candidates)
1. **Recurring tasks** — `AddRecurringTask(name, interval, mode)` re-arms after each fire until `RemoveTask`.
2. ~~Task payload~~ — **done**: per-task `float`/`Form` payload rides the mod event (`numArg`/`sender`, FormIDs re-resolved at dispatch and `ResolveFormID`-rebased on load).
3. ~~Source attribution~~ — **done**: calling script name always recorded; defining plugin resolved from the caller's record handle or payload form (see Implementation status).
4. **Config file** — ini next to the DLL for tick interval, queue cap, log level.
5. **Integrate with a real consumer** — wire an actual mod process (e.g. a BindingUpdated-style quest loop) to replace its bindc scheduler as the proof case.
6. **Packaging** — README for the mod folder, version bump in `CMakeLists`, Nexus-ready archive.

## Risks & mitigations
- **Co-save corruption** → bounds-check count/length; fail-soft per record.
- **Thread safety** → mutex around the queue; VM-facing dispatch marshalled to the game thread; worker loop only reads globals.
- **Unique-ID collision** → distinctive 4CC `TKSC`; log on collision.
- **Framework not installed** → `IsInstalled()` guard; scheduler works without the UI.
- **Handler arity mismatch silently no-ops** → pinned 4-parameter signature above; verify with the example script.
- **Never self-requeue `AddTask`** (hard-freeze hazard) — pacing via worker sleep only.

## Out of scope (v1)
- Recurring tasks, priorities, companion ESP, MCM. (Per-mod ownership: added in v2 as Source Mod/Script attribution.)
