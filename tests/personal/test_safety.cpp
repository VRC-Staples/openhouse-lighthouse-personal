// Runs without VR hardware, SteamVR, or third-party libraries. All files are temporary.
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "driver_files.hpp"
#include "common/net.hpp"
#include "transport/usb/wireless_decoder.hpp"

#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(#expr); } while (false)
namespace {
namespace fs = std::filesystem;
using lh::files::Read;
using lh::files::AtomicWrite;

template <class F> void Refuses(F action) {
  bool refused = false;
  try { action(); } catch (const std::exception&) { refused = true; }
  CHECK(refused);
}

std::string Image(const std::string& marker) {
  std::string bytes(256, '\0');
  bytes.replace(0, 2, "MZ");
  const uint32_t pe = 128;
  const uint16_t machine = 0x8664, flags = 0x2000;
  std::memcpy(bytes.data() + 60, &pe, sizeof pe);
  bytes.replace(pe, 4, std::string("PE\0\0", 4));
  std::memcpy(bytes.data() + pe + 4, &machine, sizeof machine);
  std::memcpy(bytes.data() + pe + 22, &flags, sizeof flags);
  bytes += "HmdDriverFactory " + marker;
  return bytes;
}

void FilenameBoundary() {
  for (const char* name : {"LHR-1234ABCD", "LHB_42", "device123"})
    CHECK(lh::files::SafeDeviceName(name));
  for (const char* name : {"", "..", "../victim", "..\\victim", "C:\\victim", "x:y",
                          "CON", "com1", "NUL", "x.json", "x/other", "x ", "x\n"})
    CHECK(!lh::files::SafeDeviceName(name));
  CHECK(!lh::files::SafeDeviceName(std::string("x\0y", 3)));
}

void InstallerRecovery(const fs::path& dir) {
  const auto source = dir / "package.dll", target = dir / "driver.dll", backup = dir / "stock";
  const auto stock = Image("stock fixture"), personal = Image(openhouse::installer::kPersonal);
  AtomicWrite(source, personal);
  AtomicWrite(target, stock);
  openhouse::installer::Install(source, target, backup);
  CHECK(Read(target) == personal && Read(backup) == stock);
  openhouse::installer::Install(source, target, backup);
  openhouse::installer::Restore(target, backup);
  CHECK(Read(target) == stock && Read(backup) == stock);
  AtomicWrite(target, Image("new stock fixture"));
  Refuses([&] { openhouse::installer::Install(source, target, backup); });
  CHECK(Read(target) == Image("new stock fixture") && Read(backup) == stock);
  AtomicWrite(target, "damaged DLL");
  Refuses([&] { openhouse::installer::Restore(target, backup); });
  Refuses([&] { openhouse::installer::Install(source, target, backup); });
  CHECK(Read(target) == "damaged DLL" && Read(backup) == stock);
  AtomicWrite(source, "invalid package");
  Refuses([&] { openhouse::installer::Install(source, target, backup); });
  CHECK(Read(backup) == stock);
}

void LockedFilePreservation(const fs::path& dir) {
  {
    lh::files::StagingDirectory stage(dir);
    const auto target = dir / "new-report.zip";
    AtomicWrite(stage.Payload(), "complete report");
    lh::files::PublishNew(stage.Payload(), target);
    CHECK(Read(target) == "complete report");
    AtomicWrite(stage.Payload(), "replacement report");
    Refuses([&] { lh::files::PublishNew(stage.Payload(), target); });
    CHECK(Read(target) == "complete report");
    CHECK(Read(stage.Payload()) == "replacement report");
  }
#ifdef _WIN32
  const auto file = dir / "locked.json";
  AtomicWrite(file, "original");
  HANDLE lock = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(lock != INVALID_HANDLE_VALUE);
  Refuses([&] { AtomicWrite(file, "replacement"); });
  CHECK(Read(file) == "original");
  CloseHandle(lock);
  lock = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  CHECK(lock != INVALID_HANDLE_VALUE);
  Refuses([&] { Read(file); });
  Refuses([&] { lh::files::BackupOnce(file, dir / "unreadable.backup"); });
  CloseHandle(lock);
  CHECK(Read(file) == "original" && !fs::exists(dir / "unreadable.backup"));
#endif
}

void DecoderBounds() {
  lh::usb::WirelessPacket packet;
  lh::usb::LegacyDecoder decoder;
  CHECK(!decoder.Decode(0, nullptr, 0, &packet));
  const uint8_t battery[] = {0, 0xe1, 42};
  CHECK(!decoder.Decode(0, battery, 2, &packet));
  CHECK(decoder.Decode(0, battery, 3, &packet) && packet.battery == 42);
  uint8_t imu[15] = {0, 0xe8};
  for (int size = 2; size < 15; ++size) CHECK(!decoder.Decode(0, imu, size, &packet));
  CHECK(decoder.Decode(0, imu, 15, &packet) && packet.hasImu);
#ifdef _WIN32
  SYSTEM_INFO system{};
  GetSystemInfo(&system);
  const size_t page = system.dwPageSize;
  auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, page * 2,
      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  CHECK(memory);
  DWORD old = 0;
  CHECK(VirtualProtect(memory + page, page, PAGE_NOACCESS, &old));
  for (int n = 1; n <= 27; ++n) {
    auto* data = memory + page - n;
    for (int header = 0; header <= 255; ++header) {
      std::memset(data, 0, size_t(n));
      if (n > 1) data[1] = uint8_t(header);
      decoder.Decode(0, data, n, &packet);
    }
  }
  VirtualFree(memory, 0, MEM_RELEASE);
#endif
}

void LocalPeer() {
  using namespace lh::net;
  Socket listener = kInvalid;
  int port = 0;
  for (int candidate = 43200; candidate < 43300 && listener == kInvalid; ++candidate) {
    listener = ListenLoopback(candidate);
    if (listener != kInvalid) port = candidate;
  }
  CHECK(listener != kInvalid);
  const Socket client = ConnectLoopback(port);
  CHECK(client != kInvalid);
  Socket peer = kInvalid;
  for (int i = 0; i < 100 && peer == kInvalid; ++i) {
    peer = Accept(listener);
    if (peer == kInvalid) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(peer != kInvalid);
  CHECK(SendSome(peer, "stats\n", 6) == 6);
  char bytes[16]{};
  long received = 0;
  for (int i = 0; i < 100 && !received; ++i) {
    received = RecvSome(client, bytes, sizeof bytes);
    if (!received) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(received == 6 && std::string(bytes, 6) == "stats\n");
  Close(peer); Close(client); Close(listener);
}
}  // namespace

int main() {
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  const auto dir = temp.Payload();
  fs::create_directory(dir);
  try {
    FilenameBoundary();
    InstallerRecovery(dir);
    LockedFilePreservation(dir);
    DecoderBounds();
    LocalPeer();
    fs::remove_all(dir);
    std::cout << "PASS: filename boundaries, installer recovery, locked files, packet bounds, local peer\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << "\nTemporary test data: " << dir << "\n";
    return 1;
  }
}
