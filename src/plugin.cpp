#include <PCH.h>
#include <windows.h>
#undef GetObject  // windows.h maps GetObject -> GetObjectW under UNICODE, clashing with Variable::GetObject
#include <cctype>
#include <spdlog/sinks/basic_file_sink.h>

#include "Scheduler.h"
#include "UI.h"

namespace {
    constexpr std::uint32_t kUniqueID = 'TKSC';
    constexpr std::string_view kScriptClass = "PapyrusTaskScheduler";

    void InitializeLog() {
        auto path = SKSE::log::log_directory();
        if (!path) {
            SKSE::stl::report_and_fail("Failed to find standard logging directory");
        }
        *path /= "PapyrusTaskScheduler.log"sv;
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S] [%l] %v"s);
    }

    // Papyrus natives exposed on the hidden script class "PapyrusTaskScheduler".
    // Parameters are taken by value: CommonLib rejects reference parameters.
    // Papyrus default argument values exist only in the .psc; the compiler bakes
    // them in, so the C++ side always receives all four arguments.

    // Plugin filename for the record a script is attached to. The high byte of a
    // FormID is the compile index of its defining plugin; light (ESL) records use
    // the 0xFE marker with the light index in bits 12-23.
    std::string SourceModOfForm(RE::FormID aFormID) {
        auto* data = const_cast<RE::TESDataHandler*>(RE::TESDataHandler::GetSingleton());
        if (!data) {
            return {};
        }
        const auto indexByte = static_cast<std::uint8_t>(aFormID >> 24);
        const RE::TESFile* file = nullptr;
        if (indexByte < data->GetLoadedModCount()) {
            file = data->LookupLoadedModByIndex(indexByte);
        } else {
            file = data->LookupLoadedLightModByIndex(static_cast<std::uint16_t>((aFormID >> 12) & 0xFFF));
        }
        return file ? std::string(file->GetFilename()) : std::string("unknown");
    }

    // VMAD-attached script instances (quest fragments, activator/object property
    // scripts) are registered in the VM's attachedScripts map keyed by the owning
    // form's handle (a vanilla form's handle is its FormID). Find that handle by
    // pointer-identity against the running frame's self object.
    RE::VMHandle AttachedHandleOf(RE::BSScript::Internal::VirtualMachine* a_vm, RE::BSScript::Object* a_obj) {
        if (!a_vm || !a_obj) {
            return 0;
        }
        for (const auto& entry : a_vm->attachedScripts) {
            for (const auto& script : entry.second) {
                if (script.get() == a_obj) {
                    return entry.first;
                }
            }
        }
        return 0;
    }

    // Resolve which mod and script are calling a scheduler native. The VM hands
    // the dispatch the stack ID of the executing script, so GetStackByID gives the
    // exact caller stack. Every script instance created from a VMAD record (quest,
    // object, activator, ...) is bound to its parent form: the frame's self object
    // maps through GetBoundHandle to that record's FormID, whose high byte is the
    // defining plugin. The qf_<editorid>_<formid> name suffix is kept as a fallback.
    // Resolve which mod and script are calling a scheduler native. The VM hands
    // the dispatch the stack ID of the executing script, so GetStackByID gives the
    // exact caller stack. The caller script name is always recoverable from the
    // frame chain. The source mod is only resolvable when an instance in the chain
    // is attached to a record (VMAD quest fragments, bound object scripts); plain
    // framework instances created with new carry no record handle, so the mod
    // column then falls back to the payload form's owner, or stays empty.
    std::pair<std::string, std::string> ResolveCallerInfo(RE::BSScript::Internal::VirtualMachine* a_vm, RE::VMStackID a_stackID) {
        RE::BSScript::Stack* stack = nullptr;
        if (!a_vm || !a_vm->GetStackByID(a_stackID, &stack) || !stack || !stack->top) {
            return {};
        }

        const RE::VMHandle emptyHandle = a_vm->GetObjectHandlePolicy()->EmptyHandle();
        auto modForHandle = [](RE::VMHandle a_handle) -> std::string {
            // Vanilla form handles keep the FormID in the low 32 bits.
            if (a_handle == 0) {
                return {};
            }
            const auto* form = RE::TESForm::LookupByID(static_cast<RE::FormID>(a_handle & 0xFFFFFFFF));
            return form ? SourceModOfForm(form->GetFormID()) : std::string{};
        };

        std::string callerScript;
        std::string callerMod;
        for (const auto* frame = stack->top; frame; frame = frame->previousFrame) {
            const auto* type = frame->owningObjectType.get();
            if (!type) {
                continue;
            }
            const std::string_view scriptName = type->name;
            if (scriptName == kScriptClass) {
                continue;  // the native dispatch frame itself
            }
            if (callerScript.empty()) {
                callerScript = std::string(scriptName);
            }

            const auto obj = frame->self.GetObject();
            if (!obj) {
                continue;
            }
            RE::VMHandle handle = a_vm->GetBoundHandle(obj);
            if (handle != emptyHandle) {
                callerMod = modForHandle(handle);
                if (!callerMod.empty()) {
                    break;
                }
            }
            handle = AttachedHandleOf(a_vm, obj.get());
            if (handle != 0 && handle != emptyHandle) {
                callerMod = modForHandle(handle);
                if (!callerMod.empty()) {
                    break;
                }
            }
        }

        return {callerMod, callerScript};
    }

    bool AddDelayTask(RE::BSScript::Internal::VirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                      std::string aName, float aSeconds, float payloadNumber, RE::TESForm* payloadForm) {
        auto [sourceMod, sourceScript] = ResolveCallerInfo(a_vm, a_stackID);
        if (sourceMod.empty() && payloadForm) {
            sourceMod = SourceModOfForm(payloadForm->GetFormID());  // attribute via the payload's owner
        }
        return Scheduler::Get().AddDelayTask(aName, aSeconds, payloadNumber, payloadForm, sourceMod, sourceScript);
    }

    bool AddGameTimeTask(RE::BSScript::Internal::VirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                         std::string aName, float aGameHours, float payloadNumber, RE::TESForm* payloadForm) {
        auto [sourceMod, sourceScript] = ResolveCallerInfo(a_vm, a_stackID);
        if (sourceMod.empty() && payloadForm) {
            sourceMod = SourceModOfForm(payloadForm->GetFormID());  // attribute via the payload's owner
        }
        return Scheduler::Get().AddGameTimeTask(aName, aGameHours, payloadNumber, payloadForm, sourceMod, sourceScript);
    }

    bool RemoveTask(RE::StaticFunctionTag*, std::string aName) {
        return Scheduler::Get().RemoveTask(aName);
    }

    bool HasTask(RE::StaticFunctionTag*, std::string aName) {
        return Scheduler::Get().HasTask(aName);
    }

    float GetRemainingSeconds(RE::StaticFunctionTag*, std::string aName) {
        return static_cast<float>(Scheduler::Get().GetRemainingSeconds(aName));
    }

    float GetRemainingGameHours(RE::StaticFunctionTag*, std::string aName) {
        return static_cast<float>(Scheduler::Get().GetRemainingGameHours(aName));
    }

    bool BindNatives(RE::BSScript::IVirtualMachine* a_vm) {
        a_vm->RegisterFunction("AddDelayTask", kScriptClass, AddDelayTask);
        a_vm->RegisterFunction("AddGameTimeTask", kScriptClass, AddGameTimeTask);
        a_vm->RegisterFunction("RemoveTask", kScriptClass, RemoveTask);
        a_vm->RegisterFunction("HasTask", kScriptClass, HasTask);
        a_vm->RegisterFunction("GetRemainingSeconds", kScriptClass, GetRemainingSeconds);
        a_vm->RegisterFunction("GetRemainingGameHours", kScriptClass, GetRemainingGameHours);
        return true;
    }

    void SaveCallback(SKSE::SerializationInterface* a_intfc) {
        Scheduler::Get().OnSave(a_intfc);
    }

    void LoadCallback(SKSE::SerializationInterface* a_intfc) {
        Scheduler::Get().OnLoad(a_intfc);
    }

    void RevertCallback(SKSE::SerializationInterface* a_intfc) {
        Scheduler::Get().OnRevert();
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
        Scheduler::Get().Stop();
    }
    return TRUE;
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);
    InitializeLog();

    const auto* serialization = SKSE::GetSerializationInterface();
    serialization->SetUniqueID(kUniqueID);
    serialization->SetSaveCallback(SaveCallback);
    serialization->SetLoadCallback(LoadCallback);
    serialization->SetRevertCallback(RevertCallback);

    SKSE::GetPapyrusInterface()->Register(BindNatives);

    const auto* messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
        if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
            SKSE::log::info("PapyrusTaskScheduler v0.1.0 ready: starting dispatcher thread");
            Scheduler::Get().Start();
            TaskSchedulerUI::Register();
        }
    });

    return true;
}
