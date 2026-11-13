#pragma once
#include <Eigen/Geometry>
#include "lh/tracker.hpp"

namespace lh::tracking {
inline RigidPose WorldFromSteamBase(const RigidPose& baseFromWorld) {
  const auto& o = baseFromWorld.orientation;
  const auto q = Eigen::Quaterniond(o[0], o[1], o[2], o[3]).conjugate();
  const Eigen::Vector3d p = -(q * Eigen::Vector3d(baseFromWorld.position.data()));
  return {{p.x(), p.y(), p.z()}, {q.w(), q.x(), q.y(), q.z()}};
}
}  // namespace lh::tracking
