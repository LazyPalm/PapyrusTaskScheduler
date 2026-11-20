Scriptname ExampleTaskConsumer extends Quest Conditional

Event OnInit()
    Self.RegisterForModEvent("Example_MiningProcess", "OnTaskDue")
    Self.RegisterForModEvent("Example_GardenCheck", "OnTaskDue")
    ; Fires when the user cancels any task from the scheduler's Task Viewer menu.
    ; The canceled task name arrives as the event source string.
    Self.RegisterForModEvent("PapyrusTaskSchedulerCancel", "OnTaskCancelled")

    PapyrusTaskScheduler.AddDelayTask("Example_MiningProcess", 60.0, 42.0, Self)
    PapyrusTaskScheduler.AddGameTimeTask("Example_GardenCheck", 3.0)
endEvent

Event OnTaskDue(String aEventName, String aTaskName, Float aNumArg, Form aSender)
    Debug.Trace("ExampleTaskConsumer: task fired: " + aTaskName)
    Debug.Trace("  payload number: " + aNumArg)
    Debug.Trace("  payload sender: " + aSender)
endEvent

Event OnTaskCancelled(String aEventName, String aTaskName, Float aNumArg, Form aSender)
    if aTaskName == "Example_MiningProcess"
        Debug.Trace("ExampleTaskConsumer: mining process was canceled by the user; cleaning up.")
    endif
endEvent
