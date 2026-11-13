#include <gtest/gtest.h>
#include "openvr/glb_accessor.hpp"
#include "openvr/hand_skeleton.hpp"
#include "platform/safe_files.hpp"

namespace {
using json = nlohmann::json;
using namespace lh::openvr;

json Document() {
  return {{"bufferViews", {{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 24}}}},
          {"accessors", {{{"bufferView", 0}, {"componentType", 5126},
                          {"type", "VEC3"}, {"count", 2}}}}};
}

TEST(GlbBounds, ReadsValidFramesAndClampsTheRequestedFrame) {
  std::vector<uint8_t> bytes(24);
  const float data[6] = {1, 2, 3, 4, 5, 6};
  std::memcpy(bytes.data(), data, sizeof data);
  EXPECT_EQ(GlbValues(Document(), bytes, 0, 24, 0, 0, 3), (std::vector<float>{1, 2, 3}));
  EXPECT_EQ(GlbValues(Document(), bytes, 0, 24, 0, 999, 3), (std::vector<float>{4, 5, 6}));
}

TEST(GlbBounds, RejectsWrappedOffsetsCountsStridesAndTruncatedViews) {
  const std::vector<uint8_t> bytes(24);
  auto reject = [&](const json& document) {
    EXPECT_THROW(GlbValues(document, bytes, 0, 24, 0, 0, 3), std::exception);
  };
  auto j = Document(); j["bufferViews"][0]["byteOffset"] = UINT64_MAX - 3; reject(j);
  j = Document(); j["accessors"][0]["byteOffset"] = UINT64_MAX; reject(j);
  j = Document(); j["accessors"][0]["count"] = 0; reject(j);
  j = Document(); j["accessors"][0]["count"] = UINT64_MAX; reject(j);
  j = Document(); j["bufferViews"][0]["byteStride"] = UINT64_MAX; reject(j);
  j = Document(); j["bufferViews"][0]["byteStride"] = 0; reject(j);
  j = Document(); j["bufferViews"][0]["byteLength"] = 23; reject(j);
  j = Document(); j["bufferViews"][0]["byteOffset"] = -1; reject(j);
  EXPECT_THROW(GlbValues(Document(), bytes, UINT64_MAX, 24, 0, 0, 3), std::exception);
}

std::string GlbFile(const json& j, size_t binBytes) {
  std::string text = j.dump();
  while (text.size() % 4) text += ' ';
  std::string file;
  auto word = [&](uint32_t value) { file.append(reinterpret_cast<const char*>(&value), 4); };
  word(0x46546c67); word(2); word(uint32_t(28 + text.size() + binBytes));
  word(uint32_t(text.size())); word(0x4e4f534a); file += text;
  word(uint32_t(binBytes)); word(0x004e4942); file.append(binBytes, '\0');
  return file;
}

TEST(GlbBounds, SkeletonLoaderRejectsMalformedAccessorWithoutInstalledValveAssets) {
  json j = Document();
  j["nodes"] = json::array();
  for (int i = 0; i < 32; ++i) j["nodes"].push_back({{"name", i == 1 ? "REF:Root" : "bone"}});
  j["animations"] = {{{"samplers", {{{"input", 0}, {"output", 0}}}},
                        {"channels", {{{"sampler", 0}, {"target", {{"node", 1}, {"path", "translation"}}}}}}};
  j["bufferViews"][0]["byteOffset"] = UINT64_MAX - 3;
  lh::files::StagingDirectory temp(std::filesystem::temp_directory_path());
  lh::files::AtomicWrite(temp.Payload(), GlbFile(j, 24));
  HandSkeleton skeleton;
  std::string error;
  EXPECT_FALSE(skeleton.Load(temp.Payload().string(), false, &error));
  EXPECT_NE(error.find("bad accessor bounds"), std::string::npos) << error;
}

TEST(GlbBounds, LoadsFiniteSyntheticAnimationAndRejectsTruncatedBin) {
  json j;
  j["nodes"] = json::array();
  for (int i = 0; i < 32; ++i) j["nodes"].push_back({{"name", i == 1 ? "REF:Root" : "bone"}});
  j["bufferViews"] = {{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 152}},
                      {{"buffer", 0}, {"byteOffset", 152}, {"byteLength", 456}}};
  j["accessors"] = {{{"bufferView", 0}, {"componentType", 5126}, {"type", "SCALAR"}, {"count", 38}},
                    {{"bufferView", 1}, {"componentType", 5126}, {"type", "VEC3"}, {"count", 38}}};
  j["animations"] = {{{"samplers", {{{"input", 0}, {"output", 1}}}},
                      {"channels", {{{"sampler", 0}, {"target", {{"node", 1}, {"path", "translation"}}}}}}};
  lh::files::StagingDirectory temp(std::filesystem::temp_directory_path());
  auto bytes = GlbFile(j, 608);
  lh::files::AtomicWrite(temp.Payload(), bytes);
  HandSkeleton skeleton;
  std::string error;
  ASSERT_TRUE(skeleton.Load(temp.Payload().string(), false, &error)) << error;
  bytes.pop_back();
  const uint32_t size = uint32_t(bytes.size());
  std::memcpy(bytes.data() + 8, &size, sizeof size);
  lh::files::AtomicWrite(temp.Payload(), bytes);
  EXPECT_FALSE(skeleton.Load(temp.Payload().string(), false, &error));
  EXPECT_NE(error.find("truncated GLB BIN"), std::string::npos) << error;
}
}  // namespace
