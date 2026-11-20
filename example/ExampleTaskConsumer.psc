Scriptname ExampleTaskConsumer extends Quest Conditional

Event OnInit()
    Self.RegisterForModEvent("Example_MiningProcess", "OnTaskDue")
    Self.RegisterForModEvent("Example_GardenCheck", "OnTaskDue")

    TaskScheduler.AddDelayTask("Example_MiningProcess", 60.0)
    TaskScheduler.AddGameTimeTask("Example_GardenCheck", 3.0)
endEvent

Event OnTaskDue(String aEventName, String aTaskName, Float aNumArg, Form aSender)
    Debug.Trace("ExampleTaskConsumer: task fired: " + aTaskName)
endEvent
