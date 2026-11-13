#include "gui_fixture.hpp"

#include <nlohmann/json.hpp>
#include <cmath>

using openhouse::viewer::test::activeTrace;

// Bounds/labels only: these hooks do not replace ImGui rendering or input behavior.
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& bb,
                                 const ImGuiLastItemData*) {
  if (activeTrace) activeTrace->bounds[id] = bb;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label,
                                  ImGuiItemStatusFlags) {
  if (activeTrace && activeTrace->bounds.count(id))
    activeTrace->labels[label] = activeTrace->bounds.at(id);
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return ""; }

namespace openhouse::viewer::test {

void ViewerGui::SetUp() {
  FakeClientTest::SetUp();
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  io.UserData = this;
  io.DisplaySize = ImVec2(1400, 800);
  io.DeltaTime = 1.0f / 60;
  unsigned char* pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  io.Fonts->SetTexID(ImTextureID(1));  // CPU draw data only; no renderer/backend is loaded.
  ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext* ctx, const char* text) {
    static_cast<ViewerGui*>(ctx->IO.UserData)->clipboard = text;
  };
  activeTrace = &trace;
  ImGui::GetCurrentContext()->TestEngineHookItems = true;
  Hello();
  Frame();
  Frame();
}

void ViewerGui::TearDown() {
  activeTrace = nullptr;
  ImGui::DestroyContext();
  FakeClientTest::TearDown();
}

void ViewerGui::Frame() {
  trace.bounds.clear();
  trace.labels.clear();
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(1200, 600));
  ImGui::Begin("ViewerTest", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
  view.Draw(*client);
  hashGlyph = *ImGui::GetFontBaked()->FindGlyph('#');
  zGlyph = *ImGui::GetFontBaked()->FindGlyph('z');
  ImGuiWindow* parent = ImGui::GetCurrentWindow();
  logDrawList = nullptr;
  for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
    if (window->ParentWindow == parent && window->ChildId == parent->GetID("lines"))
      logDrawList = window->DrawList;
  ImGui::End();
  ImGui::Render();
}

void ViewerGui::Click(const std::string& label) {
  ASSERT_TRUE(trace.labels.count(label)) << "Missing widget: " << label;
  const ImRect rect = trace.labels.at(label);
  auto& io = ImGui::GetIO();
  io.AddMousePosEvent((rect.Min.x + rect.Max.x) / 2, (rect.Min.y + rect.Max.y) / 2);
  io.AddMouseButtonEvent(0, true);
  Frame();
  io.AddMouseButtonEvent(0, false);
  Frame();
}

void ViewerGui::Log(const std::string& text) {
  Feed(nlohmann::json{{"type", "log"}, {"text", text}}.dump() + "\n");
}

int ViewerGui::GlyphCount(const ImDrawList* list, const ImFontGlyph& glyph) const {
  if (!list) return 0;
  int count = 0;
  const ImVec2 expected[] = {{glyph.U0, glyph.V0}, {glyph.U1, glyph.V0},
                             {glyph.U1, glyph.V1}, {glyph.U0, glyph.V1}};
  for (int i = 0; i + 3 < list->VtxBuffer.Size; ++i) {
    bool match = true;
    for (int j = 0; j < 4; ++j) {
      const ImVec2 uv = list->VtxBuffer[i + j].uv;
      match = match && std::abs(uv.x - expected[j].x) < 1e-6f &&
              std::abs(uv.y - expected[j].y) < 1e-6f;
    }
    if (match) ++count;
  }
  return count;
}

ImDrawList* ViewerGui::PopupDrawList() const {
  for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
    if (window->Active && (window->Flags & ImGuiWindowFlags_Popup)) return window->DrawList;
  return nullptr;
}

}  // namespace openhouse::viewer::test
