#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "openvr/steam_rooms.hpp"

TEST(SteamRoomMetadata, SkipsWrongTypeOwnershipMarkers) {
  using json = nlohmann::json;
  for (const json marker : {json("bad"), json(1), json(1.5), json(nullptr),
                            json::array(), json::object()}) {
    const json good = {{"id", "keep"}};
    const json invalid = {{"id", "bad"}, {"openhouse", marker}};
    const json input = {{"known_universes", json::array({good, invalid})}};
    std::string filtered;
    ASSERT_NO_THROW(filtered = lh::openvr::ForeignUniverses(input.dump(), {"keep", "bad"}));
    EXPECT_EQ(json::parse(filtered).at("known_universes"), json::array({good}));
  }
}

TEST(SteamRoomMetadata, PreservesForeignBooleanFalseAndExcludesOwnedRooms) {
  const std::string input = R"({"known_universes":[
    {"id":"foreign","openhouse":false},{"id":"owned","openhouse":true}]})";
  const auto kept = nlohmann::json::parse(lh::openvr::ForeignUniverses(input, {"foreign", "owned"}));
  ASSERT_EQ(kept.at("known_universes").size(), 1u);
  EXPECT_EQ(kept.at("known_universes")[0].at("id"), "foreign");
}
