#include "log_view.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <imgui.h>
#include <optional>
#include <vector>

namespace openhouse::viewer {
namespace {
ImVec4 LevelColor(lh::LogLevel level) {
  switch (level) {
    case lh::LogLevel::Debug: return {0.55f, 0.58f, 0.62f, 1};
    case lh::LogLevel::Info: return {0.86f, 0.88f, 0.90f, 1};
    case lh::LogLevel::Warn: return {1.00f, 0.78f, 0.30f, 1};
    case lh::LogLevel::Error: return {1.00f, 0.42f, 0.40f, 1};
  }
  return {1, 1, 1, 1};
}

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}

std::vector<const LogLine*> ShownLogs(const TelemetryClient& client, const LogFilter& filter,
                                     std::optional<uint64_t> pausedAt) {
  const auto& logs = client.Logs();
  size_t end = logs.size();
  if (pausedAt) {
    const uint64_t first = client.LogsReceived() - logs.size();
    end = *pausedAt <= first ? 0 : size_t(std::min<uint64_t>(*pausedAt - first, logs.size()));
  }
  std::vector<const LogLine*> shown;
  shown.reserve(end);
  for (size_t i = 0; i < end; ++i)
    if (filter.Shows(logs[i])) shown.push_back(&logs[i]);
  return shown;
}

// ImGui labels interpret ##/###. Keep the item ID separate from literal message text.
bool LiteralSelectable(const char* id, const std::string& text, ImGuiSelectableFlags flags = 0) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const ImVec2 size = ImGui::CalcTextSize(text.c_str(), nullptr, false);
  const bool clicked = ImGui::Selectable(id, false, flags, size);
  ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
  return clicked;  // AddText leaves the selectable as the last item for its context menu.
}
}  // namespace

bool LogFilter::Shows(const LogLine& line) const {
  switch (line.level) {
    case lh::LogLevel::Debug: if (!debug) return false; break;
    case lh::LogLevel::Info: if (!info) return false; break;
    case lh::LogLevel::Warn: if (!warn) return false; break;
    case lh::LogLevel::Error: if (!error) return false; break;
  }
  if (hideMulti && line.text.find("[multi]") != std::string::npos) return false;
  if (hideLh2 && line.text.find("[lh2]") != std::string::npos) return false;
  if (!hidden.empty() && hidden.count(Shape(line.text))) return false;
  if (!text.empty() && Lower(line.text).find(text) == std::string::npos && Lower(line.serial).find(text) == std::string::npos)
    return false;
  return true;
}

bool LogView::DrawControls(TelemetryClient& client) {
  int counts[4] = {};
  for (const auto& l : client.Logs()) ++counts[int(l.level)];
  char label[48];
  std::snprintf(label, sizeof label, "ERR (%d)", counts[3]);
  ImGui::Checkbox(label, &filter_.error);
  ImGui::SameLine();
  std::snprintf(label, sizeof label, "WARN (%d)", counts[2]);
  ImGui::Checkbox(label, &filter_.warn);
  ImGui::SameLine();
  std::snprintf(label, sizeof label, "INFO (%d)", counts[1]);
  ImGui::Checkbox(label, &filter_.info);
  ImGui::SameLine();
  std::snprintf(label, sizeof label, "DEBUG (%d)", counts[0]);
  ImGui::Checkbox(label, &filter_.debug);
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::Checkbox("Hide multi-station chatter", &filter_.hideMulti);
  ImGui::SameLine();
  ImGui::Checkbox("Hide 2.0 light chatter", &filter_.hideLh2);
  ImGui::SetNextItemWidth(240);
  if (ImGui::InputTextWithHint("##search", "Search messages or serials", search_, sizeof search_)) filter_.text = Lower(search_);
  ImGui::SameLine();
  ImGui::Checkbox("Auto-scroll", &autoScroll_);
  ImGui::SameLine();
  if (ImGui::Checkbox("Pause", &paused_) && paused_) pausedAt_ = client.LogsReceived();
  ImGui::SameLine();
  if (ImGui::Button("Clear")) { client.ClearLogs(); pausedAt_ = client.LogsReceived(); }
  ImGui::SameLine();
  return ImGui::Button("Copy shown");
}

void LogView::DrawHiddenKinds() {
  if (filter_.hidden.empty()) return;
  ImGui::SameLine();
  char label[48];
  std::snprintf(label, sizeof label, "Hidden kinds (%zu)", filter_.hidden.size());
  if (ImGui::Button(label)) ImGui::OpenPopup("hidden");
  if (ImGui::BeginPopup("hidden")) {
    ImGui::TextDisabled("Click one to show it again");
    for (auto it = filter_.hidden.begin(); it != filter_.hidden.end();) {
      ImGui::PushID(static_cast<const void*>(&*it));
      const bool clicked = LiteralSelectable("##kind", *it);
      ImGui::PopID();
      if (clicked) it = filter_.hidden.erase(it);
      else ++it;
    }
    if (ImGui::Button("Show all again")) filter_.hidden.clear();
    ImGui::EndPopup();
  }
}

void LogView::DrawLines(const std::vector<const LogLine*>& shown) {
  ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
  ImGuiListClipper clipper;
  clipper.Begin(int(shown.size()));
  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
      const LogLine& l = *shown[size_t(i)];
      ImGui::PushID(i);
      ImGui::TextColored(LevelColor(l.level), "%-5s", lh::LogLevelName(l.level));
      ImGui::SameLine();
      LiteralSelectable("##line", l.text, ImGuiSelectableFlags_AllowOverlap);
      if (ImGui::BeginPopupContextItem("line")) {
        if (ImGui::MenuItem("Hide messages like this")) filter_.hidden.insert(LogFilter::Shape(l.text));
        if (ImGui::MenuItem("Copy")) ImGui::SetClipboardText(l.text.c_str());
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
  }
  if (autoScroll_ && !paused_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
}

void LogView::Draw(TelemetryClient& client) {
  const bool copy = DrawControls(client);
  DrawHiddenKinds();
  const auto shown = ShownLogs(client, filter_, paused_ ? std::optional(pausedAt_) : std::nullopt);
  if (copy) {
    std::string text;
    for (const auto* line : shown)
      text += std::string("[") + lh::LogLevelName(line->level) + "] " + line->text + "\n";
    ImGui::SetClipboardText(text.c_str());
  }
  DrawLines(shown);
}

}  // namespace openhouse::viewer
