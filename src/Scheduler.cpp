#include "Scheduler.h"

namespace {
    constexpr std::uint32_t kRecordTasks = 'TASK';
    constexpr std::uint32_t kFormatVersion = 3;  // 1: no payload, 2: payload fields added, 3: source mod/script added
    constexpr std::uint16_t kMaxSourceLength = 128;  // compiled script names are longer than task names
}

Scheduler& Scheduler::Get() {
    static Scheduler instance;
    return instance;
}

double Scheduler::CurrentGameHours() {
    auto* data = const_cast<RE::TESDataHandler*>(RE::TESDataHandler::GetSingleton());
    if (!data) {
        return 0.0;
    }
    const auto* hour = data->LookupForm<RE::TESGlobal>(56, "Skyrim.esm");  // GameHour (0x000038)
    const auto* days = data->LookupForm<RE::TESGlobal>(57, "Skyrim.esm");  // GameDaysPassed (0x000039)
    if (!hour || !days) {
        return 0.0;
    }
    return static_cast<double>(days->value) * 24.0 + static_cast<double>(hour->value);
}

bool Scheduler::AddDelayTask(std::string_view aName, double aSeconds, float aPayloadNumber, RE::TESForm* aPayloadForm, std::string_view aSourceMod, std::string_view aSourceScript) {
    if (aName.empty() || aName.size() > kMaxNameLength) {
        SKSE::log::warn("PapyrusTaskScheduler: invalid task name '{}'", aName);
        return false;
    }
    std::lock_guard lock(m_mutex);
    if (m_tasks.size() >= kMaxTasks) {
        SKSE::log::warn("PapyrusTaskScheduler: task queue full ({}), rejecting '{}'", kMaxTasks, aName);
        return false;
    }
    m_tasks[std::string(aName)] = Task{
        std::string(aName), TaskMode::Delay, aSeconds < 0.0 ? 0.0 : aSeconds,
        aPayloadNumber, aPayloadForm ? aPayloadForm->GetFormID() : 0, std::string(aSourceMod), std::string(aSourceScript)};
    m_stopCond.notify_one();
    return true;
}

bool Scheduler::AddGameTimeTask(std::string_view aName, double aGameHours, float aPayloadNumber, RE::TESForm* aPayloadForm, std::string_view aSourceMod, std::string_view aSourceScript) {
    if (aName.empty() || aName.size() > kMaxNameLength) {
        SKSE::log::warn("PapyrusTaskScheduler: invalid task name '{}'", aName);
        return false;
    }
    std::lock_guard lock(m_mutex);
    if (m_tasks.size() >= kMaxTasks) {
        SKSE::log::warn("PapyrusTaskScheduler: task queue full ({}), rejecting '{}'", kMaxTasks, aName);
        return false;
    }
    const double offset = aGameHours < 0.0 ? 0.0 : aGameHours;
    m_tasks[std::string(aName)] = Task{
        std::string(aName), TaskMode::GameTime, CurrentGameHours() + offset,
        aPayloadNumber, aPayloadForm ? aPayloadForm->GetFormID() : 0, std::string(aSourceMod), std::string(aSourceScript)};
    m_stopCond.notify_one();
    return true;
}

bool Scheduler::RemoveTask(std::string_view aName) {
    std::lock_guard lock(m_mutex);
    return m_tasks.erase(std::string(aName)) > 0;
}

bool Scheduler::CancelTask(std::string_view aName) {
    Task task;
    bool found = false;
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_tasks.find(std::string(aName));
        if (it != m_tasks.end()) {
            task = it->second;
            m_tasks.erase(it);
            found = true;
        }
    }
    if (found) {
        SKSE::log::info("PapyrusTaskScheduler: cancelled task '{}' (source: {})", task.name, task.sourceMod.empty() ? "unknown" : task.sourceMod);
        DispatchModEvent("PapyrusTaskSchedulerCancel", task.name, task.payloadNumber, task.payloadFormID);
    }
    return found;
}

bool Scheduler::HasTask(std::string_view aName) {
    std::lock_guard lock(m_mutex);
    return m_tasks.find(std::string(aName)) != m_tasks.end();
}

double Scheduler::GetRemainingSeconds(std::string_view aName) {
    std::lock_guard lock(m_mutex);
    const auto it = m_tasks.find(std::string(aName));
    if (it == m_tasks.end() || it->second.mode != TaskMode::Delay) {
        return -1.0;
    }
    return it->second.deadline;
}

double Scheduler::GetRemainingGameHours(std::string_view aName) {
    std::lock_guard lock(m_mutex);
    const auto it = m_tasks.find(std::string(aName));
    if (it == m_tasks.end() || it->second.mode != TaskMode::GameTime) {
        return -1.0;
    }
    return it->second.deadline - CurrentGameHours();
}

std::vector<Task> Scheduler::Snapshot() {
    std::lock_guard lock(m_mutex);
    std::vector<Task> tasks;
    tasks.reserve(m_tasks.size());
    for (const auto& [name, task] : m_tasks) {
        tasks.push_back(task);
    }
    std::sort(tasks.begin(), tasks.end(), [](const Task& a, const Task& b) {
        return a.deadline < b.deadline;
    });
    return tasks;
}

void Scheduler::Start() {
    if (m_running.load()) {
        return;
    }
    m_stop.store(false);
    m_running.store(true);
    m_worker = std::thread([this]() {
        Tick();
    });
}

void Scheduler::Stop() {
    m_stop.store(true);
    m_stopCond.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_running.store(false);
}

void Scheduler::Tick() {
    auto last = std::chrono::steady_clock::now();
    while (!m_stop.load()) {
        std::vector<Task> due;
        {
            std::unique_lock lock(m_mutex);
            if (m_tasks.empty()) {
                // Idle: long wait, but wake immediately when a task is added or Stop() is called.
                m_stopCond.wait_for(lock, std::chrono::seconds(5), [this] {
                    return m_stop.load() || !m_tasks.empty();
                });
                if (m_stop.load() || m_tasks.empty()) {
                    continue;
                }
                // Woken by a fresh task: start the clock at wake time so the idle
                // period is not charged against its countdown.
                last = std::chrono::steady_clock::now();
            } else {
                // Active tasks: one tick per second.
                m_stopCond.wait_for(lock, std::chrono::seconds(1), [this] { return m_stop.load(); });
                if (m_stop.load()) {
                    continue;
                }
            }

            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - last).count();
            last = now;

            const double gameHours = CurrentGameHours();
            for (auto& [name, task] : m_tasks) {
                if (task.mode == TaskMode::Delay) {
                    task.deadline -= elapsed;
                    if (task.deadline <= 0.0) {
                        due.push_back(task);
                    }
                } else if (gameHours >= task.deadline) {
                    due.push_back(task);
                }
            }
            for (const auto& task : due) {
                m_tasks.erase(task.name);
            }
        }

        for (const auto& task : due) {
            // Marshal the dispatch onto the game thread. One-shot task only: never self-requeue.
            DispatchModEvent(task.name, task.name, task.payloadNumber, task.payloadFormID);
            SKSE::log::info("PapyrusTaskScheduler: fired task '{}'", task.name);
        }
    }
}

void Scheduler::DispatchModEvent(std::string_view aEventName, std::string_view aEventSource, float aArg, RE::FormID aPayloadFormID) {
    std::string eventName(aEventName);
    std::string eventSource(aEventSource);
    SKSE::GetTaskInterface()->AddTask([eventName, eventSource, aArg, aPayloadFormID]() {
        auto* source = const_cast<RE::BSTEventSource<SKSE::ModCallbackEvent>*>(SKSE::GetModCallbackEventSource());
        if (!source) {
            return;
        }
        // Re-resolve the payload form now: it may have been deleted or
        // left the load order between scheduling and firing.
        RE::TESForm* sender = aPayloadFormID != 0 ? RE::TESForm::LookupByID(aPayloadFormID) : nullptr;
        SKSE::ModCallbackEvent event{RE::BSFixedString(eventName), RE::BSFixedString(eventSource), aArg, sender};
        source->SendEvent(&event);
    });
}

void Scheduler::OnSave(SKSE::SerializationInterface* a_intfc) {
    std::lock_guard lock(m_mutex);
    if (m_tasks.empty()) {
        return;
    }
    if (!a_intfc->OpenRecord(kRecordTasks, kFormatVersion)) {
        return;
    }
    const std::uint32_t count = static_cast<std::uint32_t>(m_tasks.size());
    a_intfc->WriteRecordData(count);
    for (const auto& [name, task] : m_tasks) {
        const std::uint16_t len = static_cast<std::uint16_t>(task.name.size());
        a_intfc->WriteRecordData(len);
        for (char c : task.name) {
            a_intfc->WriteRecordData(c);
        }
        a_intfc->WriteRecordData(static_cast<std::uint8_t>(task.mode));
        a_intfc->WriteRecordData(task.deadline);
        a_intfc->WriteRecordData(task.payloadNumber);
        a_intfc->WriteRecordData(task.payloadFormID);
        const std::uint16_t srcLen = static_cast<std::uint16_t>(task.sourceMod.size());
        a_intfc->WriteRecordData(srcLen);
        for (char c : task.sourceMod) {
            a_intfc->WriteRecordData(c);
        }
        const std::uint16_t scriptLen = static_cast<std::uint16_t>(task.sourceScript.size());
        a_intfc->WriteRecordData(scriptLen);
        for (char c : task.sourceScript) {
            a_intfc->WriteRecordData(c);
        }
    }
    SKSE::log::info("PapyrusTaskScheduler: saved {} task(s) to co-save", count);
}

void Scheduler::OnLoad(SKSE::SerializationInterface* a_intfc) {
    std::uint32_t type;
    std::uint32_t version;
    std::uint32_t length;
    while (a_intfc->GetNextRecordInfo(type, version, length)) {
        if (type != kRecordTasks || version < 1 || version > kFormatVersion) {
            continue;
        }

        std::uint32_t count;
        if (a_intfc->ReadRecordData(count) != sizeof(count) || count > kMaxTasks) {
            SKSE::log::warn("PapyrusTaskScheduler: invalid task count in co-save, skipping record");
            continue;
        }

        std::lock_guard lock(m_mutex);
        bool ok = true;
        for (std::uint32_t i = 0; i < count && ok; ++i) {
            std::uint16_t len;
            if (a_intfc->ReadRecordData(len) != sizeof(len) || len == 0 || len > kMaxNameLength) {
                ok = false;
                break;
            }
            std::string name;
            name.reserve(len);
            for (std::uint16_t j = 0; j < len; ++j) {
                char c;
                if (a_intfc->ReadRecordData(c) != sizeof(c)) {
                    ok = false;
                    break;
                }
                name.push_back(c);
            }
            std::uint8_t mode;
            double deadline;
            if (!ok || a_intfc->ReadRecordData(mode) != sizeof(mode) || mode > 1) {
                ok = false;
                break;
            }
            if (a_intfc->ReadRecordData(deadline) != sizeof(deadline)) {
                ok = false;
                break;
            }

            float payloadNumber = 0.0f;
            RE::FormID payloadFormID = 0;
            std::string sourceMod;
            std::string sourceScript;
            if (version >= 2) {
                if (a_intfc->ReadRecordData(payloadNumber) != sizeof(payloadNumber)) {
                    ok = false;
                    break;
                }
                std::uint32_t savedFormID = 0;
                if (a_intfc->ReadRecordData(savedFormID) != sizeof(savedFormID)) {
                    ok = false;
                    break;
                }
                if (savedFormID != 0) {
                    // The high byte is the load-order mod index, which shifts when
                    // the user reorders plugins; re-resolve or drop the payload form.
                    RE::FormID resolved = 0;
                    if (a_intfc->ResolveFormID(savedFormID, resolved)) {
                        payloadFormID = resolved;
                    } else {
                        SKSE::log::warn("PapyrusTaskScheduler: payload form {:08X} for task '{}' no longer in the load order", savedFormID, name);
                    }
                }
            }
            if (version >= 3) {
                std::uint16_t srcLen;
                if (a_intfc->ReadRecordData(srcLen) != sizeof(srcLen) || srcLen > kMaxNameLength) {
                    ok = false;
                    break;
                }
                sourceMod.reserve(srcLen);
                for (std::uint16_t j = 0; j < srcLen; ++j) {
                    char c;
                    if (a_intfc->ReadRecordData(c) != sizeof(c)) {
                        ok = false;
                        break;
                    }
                    sourceMod.push_back(c);
                }
                if (!ok) {
                    break;
                }
                std::uint16_t scriptLen;
                if (a_intfc->ReadRecordData(scriptLen) != sizeof(scriptLen) || scriptLen > kMaxSourceLength) {
                    ok = false;
                    break;
                }
                sourceScript.reserve(scriptLen);
                for (std::uint16_t j = 0; j < scriptLen; ++j) {
                    char c;
                    if (a_intfc->ReadRecordData(c) != sizeof(c)) {
                        ok = false;
                        break;
                    }
                    sourceScript.push_back(c);
                }
                if (!ok) {
                    break;
                }
            }
            m_tasks[name] = Task{name, static_cast<TaskMode>(mode), deadline, payloadNumber, payloadFormID, std::move(sourceMod), std::move(sourceScript)};
        }
        if (!ok) {
            SKSE::log::warn("PapyrusTaskScheduler: malformed task record in co-save, discarding the rest of the record");
        } else {
            SKSE::log::info("PapyrusTaskScheduler: loaded {} task(s) from co-save", count);
        }
    }
}

void Scheduler::OnRevert() {
    std::lock_guard lock(m_mutex);
    m_tasks.clear();
}
