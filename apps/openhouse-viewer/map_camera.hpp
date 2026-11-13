#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <imgui.h>

namespace openhouse::viewer {
using V3 = std::array<double, 3>;

// An orbit camera around a target, perspective-projected onto a rectangle.
struct Camera {
  double yaw = 0.6, pitch = 0.55, distance = 7;
  V3 target{0, 1, 0};
  ImVec2 origin, size;

  bool Project(const V3& p, ImVec2* out) const {
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const V3 eye{target[0] + distance * cp * sy, target[1] + distance * sp,
                 target[2] + distance * cp * cy};
    V3 f{target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const double fn = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (auto& c : f) c /= fn;
    const double rn = std::hypot(f[0], f[2]);
    const V3 r{-f[2] / rn, 0, f[0] / rn};
    const V3 u{r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2],
               r[0] * f[1] - r[1] * f[0]};
    const V3 d{p[0] - eye[0], p[1] - eye[1], p[2] - eye[2]};
    const double z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    if (z < 0.05) return false;
    const double x = d[0] * r[0] + d[1] * r[1] + d[2] * r[2];
    const double y = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
    const double focal = std::min(size.x, size.y) * 1.1;
    *out = ImVec2(float(origin.x + size.x / 2 + focal * x / z),
                  float(origin.y + size.y / 2 - focal * y / z));
    return true;
  }

  bool ProjectLabel(const V3& p, const ImVec2& offset, ImVec2* out) const {
    if (!Project(p, out)) return false;
    out->x += offset.x;
    out->y += offset.y;
    // Text rendering converts positions to int before horizontal clipping.
    const float limit = float(std::numeric_limits<int>::max() / 2);
    return std::isfinite(out->x) && std::isfinite(out->y) &&
           std::abs(out->x) <= limit && std::abs(out->y) <= limit &&
           out->x >= origin.x && out->x <= origin.x + size.x &&
           out->y >= origin.y && out->y <= origin.y + size.y;
  }
};
}  // namespace openhouse::viewer
