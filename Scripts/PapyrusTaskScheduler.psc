Scriptname PapyrusTaskScheduler Hidden

; Schedule a one-shot task that fires after a countdown of real seconds,
; burned only while the game process is running. Survives save/load.
; The payload (number + form) is carried to the mod event when the task fires.
Bool Function AddDelayTask(String aName, Float aSeconds, Float payloadNumber = 0.0, Form payloadForm = None) global native

; Schedule a one-shot task that fires N in-game hours from now.
Bool Function AddGameTimeTask(String aName, Float aGameHours, Float payloadNumber = 0.0, Form payloadForm = None) global native

Bool Function RemoveTask(String aName) global native
Bool Function HasTask(String aName) global native

; -1 if the task is absent or scheduled in the other mode.
Float Function GetRemainingSeconds(String aName) global native
Float Function GetRemainingGameHours(String aName) global native
