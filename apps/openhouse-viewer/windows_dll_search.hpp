#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace openhouse::viewer {
inline bool RestrictDllSearch() {
#ifdef _WIN32
  // The personal viewer uses static dependencies and Windows system DLLs only.
  return SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32) != 0;
#else
  return true;
#endif
}
}  // namespace openhouse::viewer
