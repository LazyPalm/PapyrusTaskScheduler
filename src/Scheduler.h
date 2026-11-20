#pragma once

#include <PCH.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

enum class TaskMode : std::uint8_t {
    Delay = 0,    // deadline = seconds of process uptime remaining
    GameTime = 1, // deadline = absolute total game hours (GameDaysPassed * 24 + GameHour)
};

struct Task {
    std::string name;
    TaskMode mode{TaskMode::Delay};
    double deadline{0.0};
    // Payload forms are stored as FormIDs, never raw pointers: the VM gives a
    // native no lifetime contract on its arguments, and the task may fire much
    // later (or after a save/load), so the form is re-resolved at dispatch.
    float payloadNumber{0.0f};
    RE::FormID payloadFormID{0};
    // Plugin filename of the mod whose script scheduled the task, resolved
    // from the VM call stack by the native side (empty when unresolved).
    std::string sourceMod;
    // Compiled script class of the calling script (e.g. qf_<editorid>_<formid>).
    std::string sourceScript;
};

class Scheduler {
public:
    static Scheduler& Get();

    bool AddDelayTask(std::string_view aName, double aSeconds, float aPayloadNumber, RE::TESForm* aPayloadForm, std::string_view aSourceMod, std::string_view aSourceScript);
    bool AddGameTimeTask(std::string_view aName, double aGameHours, float aPayloadNumber, RE::TESForm* aPayloadForm, std::string_view aSourceMod, std::string_view aSourceScript);
    bool RemoveTask(std::string_view aName);
    // UI-initiated cancel: removes the task and fires the "PapyrusTaskSchedulerCancel"
    // mod event (task name in eventSource, payload carried through) so the scheduling
    // mod can react to a user killing a process.
    bool CancelTask(std::string_view aName);
    bool HasTask(std::string_view aName);
    double GetRemainingSeconds(std::string_view aName);
    double GetRemainingGameHours(std::string_view aName);

    std::vector<Task> Snapshot();

    void Start();
    void Stop();

    void OnSave(SKSE::SerializationInterface* a_intfc);
    void OnLoad(SKSE::SerializationInterface* a_intfc);
    void OnRevert();

    static double CurrentGameHours();

    static constexpr std::size_t kMaxTasks = 1024;
    static constexpr std::size_t kMaxNameLength = 64;

private:
    void Tick();
    void DispatchModEvent(std::string_view aEventName, std::string_view aEventSource, float aArg, RE::FormID aPayloadFormID);

    std::mutex m_mutex;
    std::condition_variable m_stopCond;
    std::unordered_map<std::string, Task> m_tasks;
    std::thread m_worker;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
};
