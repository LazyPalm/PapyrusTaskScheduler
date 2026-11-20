# Papyrus Task Scheduler

A native SKSE plugin (CommonLibSSE-NG) that gives any mod a save-specific, one-shot task scheduler without a companion ESP, a quest script, or a `utility.Wait` loop.

## Description

Mods normally implement timed processes with a quest script that loops on `utility.Wait()` and polls an array of deadlines. That approach is capped in size, tied to a specific quest record, fragile across save/load, and burns VM cycles whether or not anything is due.

**PapyrusTaskScheduler** replaces all of that with a native queue:

- Mods schedule **named one-shot tasks** from Papyrus through a hidden script class, `PapyrusTaskScheduler`.
- A worker thread ticks the queue outside the VM: **1 s per tick while tasks are queued**, and a condition-variable wait when the queue is idle (a newly added task wakes it immediately, so idle time is never charged against a countdown).
- When a task comes due, the scheduler fires it as an **SKSE mod event** named after the task. Any mod script that called `RegisterForModEvent` for that name receives it — no polling, no dispatcher quest, no per-mod scheduling script.
- The queue is stored in the save's **SKSE co-save** (unique ID `TKSC`, record `TASK`, format v3), so it is inherently per-save: tasks survive save/quit/reload with their remaining time intact, and a new game starts with an empty queue (`OnRevert`).

Two scheduling modes are available:

| Mode | Deadline | Behaviour |
|---|---|---|
| `Delay` | seconds of **process uptime** | Countdown burns real time while the game process runs (including in menus); paused/closed game stops it. Survives save/load with the remaining seconds stored. |
| `GameTime` | absolute total **in-game hours** (`GameDaysPassed * 24 + GameHour`) | Resolved to an absolute deadline at schedule time; fires when the save's clock reaches it, or immediately on load if it has already passed. |

Payloads ride the event: each task carries a `Float` and a `Form`. Payloads are stored as **FormIDs, never pointers**, and re-resolved at dispatch — the VM makes no lifetime promise on its arguments, and a task may fire much later or after a save/load. On load the payload FormID is rebased with `ResolveFormID`, so a load-order reorder cannot corrupt the queue; a form that no longer exists arrives as `None`.

Every task also records **which mod and which script scheduled it** (resolved from the VM caller stack, with the payload form's owner as fallback), which is what the Task Viewer displays.

A **Task Viewer** page is registered in the [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) Mod Control Panel (`F1` → *Papyrus Task Scheduler* → *Task Viewer*) listing all pending tasks — name, source mod, source script, type, remaining time, payload — with a **Cancel** button per row. Cancelling fires the `PapyrusTaskSchedulerCancel` mod event so the scheduling mod can clean up. If the framework is not installed the UI is skipped and the scheduler still works.

**Limits:** 1024 tasks in the queue, 64 characters per task name.

**Tested on Skyrim SE 1.7.104** (built against the [marthofdoom CommonLibSSE-NG fork](https://github.com/marthofdoom/CommonLibSSE-NG), which carries the 1.7 relocation table; upstream 3.6.0 does not).

## Integration

### 1. Install

Enable the mod in Mod Organizer 2. It ships:

```
SKSE/Plugins/PapyrusTaskScheduler.dll
Scripts/PapyrusTaskScheduler.pex
Scripts/Source/PapyrusTaskScheduler.psc
Scripts/ExampleTaskConsumer.pex        ; demo consumer, safe to delete
Scripts/Source/ExampleTaskConsumer.psc
```

No `.esp` is required and the plugin touches no records, so it has no load-order position to maintain. SKSE is required; the SKSE Menu Framework is optional and only enables the Task Viewer.

### 2. Compile against the script class

`PapyrusTaskScheduler.pex` must be present in `Data\Scripts` (through the mod) at runtime. To **compile** a mod that calls the natives, the `.psc` has to be visible to the Creation Kit / Papyrus compiler: copy `Scripts\Source\PapyrusTaskScheduler.psc` into your own mod's `Scripts\Source`, or add the scheduler mod as a compile-time dependency. Calling an unbound native fails at compile time, not at runtime.

### 3. Schedule and consume

Task names are global across the whole load order — **prefix them with your mod's name** so you do not collide with another mod. Re-adding an existing name overwrites its deadline.

```papyrus
Scriptname MyMod_ProcessTracker extends Quest Conditional

Event OnInit()
    ; the event name is exactly the task name
    Self.RegisterForModEvent("MyMod_MiningProcess", "OnTaskDue")
    Self.RegisterForModEvent("MyMod_GardenCheck",   "OnTaskDue")
    Self.RegisterForModEvent("PapyrusTaskSchedulerCancel", "OnTaskCancelled")

    ; fire 60 real seconds from now, carrying the quest itself as the payload form
    PapyrusTaskScheduler.AddDelayTask("MyMod_MiningProcess", 60.0, 42.0, Self)

    ; fire 3 in-game hours from now
    PapyrusTaskScheduler.AddGameTimeTask("MyMod_GardenCheck", 3.0)
endEvent

Event OnTaskDue(String aEventName, String aTaskName, Float aNumArg, Form aSender)
    Debug.Trace("MyMod: task fired: " + aTaskName)
    Debug.Trace("  payload number: " + aNumArg)
    Debug.Trace("  payload sender: " + aSender)
endEvent

Event OnTaskCancelled(String aEventName, String aTaskName, Float aNumArg, Form aSender)
    if aTaskName == "MyMod_MiningProcess"
        Debug.Trace("MyMod: mining process canceled by the user; cleaning up.")
    endif
endEvent
```

Match the handler arity exactly — a mismatched `Event` signature silently never fires.

### 4. API

```
Bool  AddDelayTask(String aName, Float aSeconds, Float payloadNumber = 0.0, Form payloadForm = None) global native
Bool  AddGameTimeTask(String aName, Float aGameHours, Float payloadNumber = 0.0, Form payloadForm = None) global native
Bool  RemoveTask(String aName) global native
Bool  HasTask(String aName) global native
Float GetRemainingSeconds(String aName) global native       ; -1 if absent or scheduled in the other mode
Float GetRemainingGameHours(String aName) global native     ; -1 if absent or scheduled in the other mode
```

`Add*` returns `false` for an empty or over-long name, or when the queue is full. `RemoveTask` deletes silently and does **not** fire the cancel event — only the Task Viewer's Cancel button does, so a mod can tell a user kill from its own cleanup.

### 5. Semantics to rely on

- **One-shot.** A fired task is erased before dispatch; it never re-arms. Schedule again in the handler if you want a repeat.
- **Dispatch is marshalled to the game thread** via `SKSE::GetTaskInterface()->AddTask`, so handlers run in the VM's normal context.
- **Save-specific.** Nothing leaks between saves; loading a save with no co-save data gives an empty queue.
- **Backwards compatible.** Co-save records from format v1 and v2 (no payload, no attribution) still parse.

### 6. Build from source

Requires Visual Studio 2022 Build Tools, CMake ≥ 3.21, Ninja, and vcpkg. `vcpkg-configuration.json` routes `commonlibsse-ng` to the marthofdoom registry for 1.7.104 support.

```
build.cmd
```

Every build copies the DLL and the scripts to the `DEPLOY_FOLDER` in `CMakeLists.txt` (default `C:/skyrimmods/mo2/mods/PapyrusTaskScheduler`) — change that path for your own instance.

## License

MIT — see [LICENSE](LICENSE).
