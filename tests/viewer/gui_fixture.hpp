#pragma once

#include "fake_net.hpp"
#include "log_view.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <map>

namespace openhouse::viewer::test {

struct GuiTrace {
  std::map<ImGuiID, ImRect> bounds;
  std::map<std::string, ImRect> labels;
};
inline GuiTrace* activeTrace = nullptr;

class ViewerGui : public FakeClientTest {
 protected:
  LogView view;
  GuiTrace trace;
  std::string clipboard;
  ImDrawList* logDrawList = nullptr;
  ImFontGlyph hashGlyph{}, zGlyph{};

  void SetUp() override;
  void TearDown() override;
  void Frame();
  void Click(const std::string& label);
  void Log(const std::string& text);
  int GlyphCount(const ImDrawList* list, const ImFontGlyph& glyph) const;
  ImDrawList* PopupDrawList() const;
};

}  // namespace openhouse::viewer::test
