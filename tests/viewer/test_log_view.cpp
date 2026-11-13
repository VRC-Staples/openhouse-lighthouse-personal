#include "gui_fixture.hpp"

namespace openhouse::viewer::test {

TEST_F(ViewerGui, CopyShownExcludesMessagesReceivedAfterPause) {
  Log("visible-old");
  Frame();
  Click("Pause");
  Log("hidden-new");
  Frame();
  Click("Copy shown");
  EXPECT_EQ(clipboard, "[INFO] visible-old\n");
}

TEST_F(ViewerGui, PausedCopyUsesTheCurrentFilter) {
  Log("wanted-old");
  Log("other-old");
  Frame();
  Click("Pause");
  Log("wanted-new");
  view.Filter().text = "wanted";
  Frame();
  Click("Copy shown");
  EXPECT_EQ(clipboard, "[INFO] wanted-old\n");
}

TEST_F(ViewerGui, EvictionCannotMoveAPausedCopyToNewerRows) {
  Log("visible-old");
  Frame();
  Click("Pause");
  std::string input;
  const std::string line = "{\"type\":\"log\",\"text\":\"hidden-new\"}\n";
  for (size_t i = 0; i < TelemetryClient::kMaxLogs; ++i) input += line;
  Feed(input);
  ASSERT_EQ(client->Logs().size(), TelemetryClient::kMaxLogs);
  Frame();
  Click("Copy shown");
  EXPECT_TRUE(clipboard.empty());
}

TEST_F(ViewerGui, ClearWhilePausedDoesNotExposeNewMessagesToCopy) {
  Log("old");
  Frame();
  Click("Pause");
  Click("Clear");
  Log("hidden-new");
  Frame();
  Click("Copy shown");
  EXPECT_TRUE(clipboard.empty());
}

TEST_F(ViewerGui, UnpausingIncludesNewRowsAgain) {
  Log("old");
  Frame();
  Click("Pause");
  Log("new");
  Click("Pause");
  Click("Copy shown");
  EXPECT_EQ(clipboard, "[INFO] old\n[INFO] new\n");
}

TEST_F(ViewerGui, LogTextRendersLiteralDoubleHashesAndTheirSuffix) {
  Log("start##zz");
  Frame();
  Frame();
  ASSERT_NE(logDrawList, nullptr);
  EXPECT_EQ(GlyphCount(logDrawList, hashGlyph), 2);
  EXPECT_EQ(GlyphCount(logDrawList, zGlyph), 2);
}

TEST_F(ViewerGui, LogTextRendersLeadingTripleHashesAndTheirSuffix) {
  Log("###zz");
  Frame();
  Frame();
  ASSERT_NE(logDrawList, nullptr);
  EXPECT_EQ(GlyphCount(logDrawList, hashGlyph), 3);
  EXPECT_EQ(GlyphCount(logDrawList, zGlyph), 2);
}

TEST_F(ViewerGui, HiddenKindMenuAlsoRendersLiteralHashes) {
  view.Filter().hidden.insert("kind##zz");
  Frame();
  Click("Hidden kinds (1)");
  Frame();
  ImDrawList* popup = PopupDrawList();
  ASSERT_NE(popup, nullptr);
  EXPECT_EQ(GlyphCount(popup, hashGlyph), 2);
  EXPECT_EQ(GlyphCount(popup, zGlyph), 2);
}

}  // namespace openhouse::viewer::test
