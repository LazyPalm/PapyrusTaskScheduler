#include "UI.h"
#include "Scheduler.h"

namespace {
    std::string FormatRemaining(const Task& aTask) {
        char buf[64];
        if (aTask.mode == TaskMode::Delay) {
            std::snprintf(buf, sizeof(buf), "%.0f s", aTask.deadline);
        } else {
            std::snprintf(buf, sizeof(buf), "%.2f game h", aTask.deadline - Scheduler::CurrentGameHours());
        }
        return std::string(buf);
    }

    std::string FormatPayloadForm(const Task& aTask) {
        if (aTask.payloadFormID == 0) {
            return "None";
        }
        const auto* form = RE::TESForm::LookupByID(aTask.payloadFormID);
        if (!form) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "missing (%08X)", aTask.payloadFormID);
            return std::string(buf);
        }
        const char* editorID = form->GetFormEditorID();
        const char* name = form->GetName();
        std::string label = (editorID && editorID[0]) ? editorID : (name && name[0]) ? name : std::to_string(form->GetFormType());
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s (%08X)", label.c_str(), aTask.payloadFormID);
        return std::string(buf);
    }
}

void __stdcall TaskSchedulerUI::RenderViewer() {
    auto& scheduler = Scheduler::Get();
    const auto tasks = scheduler.Snapshot();

    ImGuiMCP::Text("Active tasks: %d", static_cast<int>(tasks.size()));
    ImGuiMCP::Separator();

    if (tasks.empty()) {
        ImGuiMCP::TextDisabled("No scheduled tasks.");
        return;
    }

    const ImGuiMCP::ImGuiTableFlags flags =
        ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_SizingStretchProp;
    if (ImGuiMCP::BeginTable("TaskSchedulerTasks", 8, flags)) {
        ImGuiMCP::TableSetupColumn("Task");
        ImGuiMCP::TableSetupColumn("Source Mod");
        ImGuiMCP::TableSetupColumn("Source Script");
        ImGuiMCP::TableSetupColumn("Type");
        ImGuiMCP::TableSetupColumn("Remaining");
        ImGuiMCP::TableSetupColumn("Payload #");
        ImGuiMCP::TableSetupColumn("Payload Form");
        ImGuiMCP::TableSetupColumn("Action");
        ImGuiMCP::TableHeadersRow();

        for (const auto& task : tasks) {
            ImGuiMCP::TableNextRow();

            ImGuiMCP::TableNextColumn();
            ImGuiMCP::Text("%s", task.name.c_str());

            ImGuiMCP::TableNextColumn();
            ImGuiMCP::Text("%s", task.sourceMod.empty() ? "unknown" : task.sourceMod.c_str());

            ImGuiMCP::TableNextColumn();
            ImGuiMCP::Text("%s", task.sourceScript.empty() ? "unknown" : task.sourceScript.c_str());

            ImGuiMCP::TableNextColumn();
            ImGuiMCP::Text("%s", task.mode == TaskMode::Delay ? "Delay" : "Game time");

            ImGuiMCP::TableNextColumn();
            ImGuiMCP::Text("%s", FormatRemaining(task).c_str());

            ImGuiMCP::TableNextColumn();
            ImGuiMCP::Text("%.2f", task.payloadNumber);

            ImGuiMCP::TableNextColumn();
            ImGuiMCP::Text("%s", FormatPayloadForm(task).c_str());

            ImGuiMCP::TableNextColumn();
            const std::string label = "Cancel##" + task.name;
            if (ImGuiMCP::Button(label.c_str())) {
                scheduler.CancelTask(task.name);
            }
        }
        ImGuiMCP::EndTable();
    }
}

void TaskSchedulerUI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        SKSE::log::info("PapyrusTaskScheduler: SKSE Menu Framework not installed; Task Viewer UI skipped.");
        return;
    }
    SKSEMenuFramework::SetSection("Papyrus Task Scheduler");
    SKSEMenuFramework::AddSectionItem("Task Viewer", TaskSchedulerUI::RenderViewer);
    SKSE::log::info("PapyrusTaskScheduler: registered Task Viewer page in the Mod Control Panel.");
}
