#include "pairing_mailbox.hpp"
#include "pairing_input.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include <lh/session.hpp>

namespace lh::openvr {
namespace {
using json = nlohmann::json;
using pairing_input::Text;

double Now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

int StockMailbox::RegisterMailbox(const char* name, uint64_t* handle, char* address, uint32_t addressSize) {
  if (name && std::strcmp(name, kPairingMailbox) == 0) return 1;  // ours answers pairing
  return real_->RegisterMailbox(name, handle, address, addressSize);
}

bool PairingMailbox::Open(vr::IVRDriverContext* context, LogFn log) {
  log_ = log;
  vr::EVRInitError error = vr::VRInitError_None;
  mailbox_ = context ? static_cast<IVRMailbox*>(context->GetGenericInterface(kMailboxVersion, &error)) : nullptr;
  if (!mailbox_ || error != vr::VRInitError_None) {
    if (log_) log_("pairing: the host has no " + std::string(kMailboxVersion) + " (error " + std::to_string(int(error)) +
                   "); SteamVR's pairing menu can't reach us");
    mailbox_ = nullptr;
    return false;
  }
  return Register();
}

bool PairingMailbox::Register() {
  char address[85] = {};
  const int r = mailbox_->RegisterMailbox(kPairingMailbox, &handle_, address, sizeof address);
  if (r != 0) {
    if (log_) log_("pairing: cannot register the " + std::string(kPairingMailbox) + " mailbox (" + std::to_string(r) + ")");
    mailbox_ = nullptr;
    handle_ = 0;
    return false;
  }
  if (log_) log_("pairing: answering SteamVR's dongle pairing (" + std::string(address) + ")");
  return true;
}

bool PairingMailbox::Read(std::string& text) {
  // A private host may count the NUL differently. Bound the scan by capacity and
  // invalidate stale bytes before the host writes, rather than trusting needed.
  std::fill(buffer_.begin(), buffer_.end(), '\xff');
  uint32_t needed = 0;
  const int r = mailbox_->ReadMessage(handle_, buffer_.data(), uint32_t(buffer_.size()), &needed);
  if (needed > buffer_.size() || (r != 0 && needed == buffer_.size())) {
    // An undersized read leaves the request queued. Reset the registration instead
    // of allocating its claimed size or retrying that same request every frame.
    if (log_) log_("pairing: oversized request; clearing queued mailbox requests");
    if (const int error = mailbox_->UndoRegisterMailbox(handle_); error != 0) {
      if (log_) log_("pairing: cannot reset mailbox (" + std::to_string(error) + "); pairing input disabled");
      mailbox_ = nullptr;
      handle_ = 0;
    } else {
      Register();  // Failure disables input; never use the unregistered handle.
    }
    return false;
  }
  if (r != 0 || needed == 0) return false;
  const size_t length = strnlen(buffer_.data(), buffer_.size());
  if (length == buffer_.size()) {
    if (log_) log_("pairing: ignoring a message without a terminator");
    text.clear();
  } else {
    text.assign(buffer_.data(), length);
  }
  return true;
}

void PairingMailbox::Reply(const std::string& address, const std::string& text) {
  if (!mailbox_ || address.empty()) return;
  if (const int r = mailbox_->SendMessage(handle_, address.c_str(), text.c_str()); r != 0 && log_)
    log_("pairing: reply to " + address + " failed (" + std::to_string(r) + ")");
}

std::string PairingMailbox::Discover(Session& session, const std::string& expected, const std::string* messageId) const {
  json j = {{"type", "discover_dongles_response"}, {"isPairing", pairing_.has_value()}, {"dongles", json::array()},
            {"expectedControllerType", expected}};
  for (const auto& r : session.Receivers())
    j["dongles"].push_back({{"serial", r.serial}, {"firmwareVersion", ""}, {"connectedSerial", r.connected}});
  if (messageId) j["message_id"] = json::parse(*messageId);
  return j.dump(-1, ' ', false, json::error_handler_t::replace);  // a receiver serial may not be UTF-8 (#45)
}

void PairingMailbox::Finish(Session& session, const std::string& result, const std::string& connected,
                            const std::string& expected) {
  if (!pairing_) return;
  json j = {{"type", "pairing_result"}, {"result", result}};
  if (!connected.empty()) j["connectedSerial"] = connected;
  if (pairing_->haveMessageId) j["message_id"] = pairing_->messageId;
  const Pairing done = *pairing_;
  pairing_.reset();
  Reply(done.returnAddress, j.dump(-1, ' ', false, json::error_handler_t::replace));
  if (done.sendStatus) Reply(done.returnAddress, Discover(session, expected, nullptr));
  if (log_) log_("pairing: receiver " + done.receiver + ": " + result + (connected.empty() ? "" : " (" + connected + ")"));
}

void PairingMailbox::StartPairing(Session& session, const json& message, const std::string& expected) {
  const std::string serial = Text(message, "serial"), address = Text(message, "returnAddress");
  const double timeout = message.contains("timeoutSeconds") && message.at("timeoutSeconds").is_number()
                             ? std::clamp(message.at("timeoutSeconds").get<double>(), 0.0, 600.0)
                             : 10.0;
  // SteamVR repeats start_pairing every 5 s while its pairing page waits.
  // Re-arm the same receiver without reporting a canceled pairing to the page.
  const bool again = pairing_ && pairing_->receiver == serial;
  if (pairing_ && !again) {
    if (!session.SetPairing(pairing_->receiver, 0)) {
      json failed = {{"type", "pairing_result"}, {"result", "failed_to_start"}};
      if (message.contains("message_id")) failed["message_id"] = message.at("message_id");
      Reply(address, failed.dump(-1, ' ', false, json::error_handler_t::replace));
      if (log_) log_("pairing: cannot stop receiver " + pairing_->receiver + "; refusing switch to " + serial);
      return;
    }
    Finish(session, "canceled", "", expected);
  }
  if (!again) {
    std::string before;
    for (const auto& rcv : session.Receivers())
      if (rcv.serial == serial) before = rcv.connected;
    pairing_ = Pairing{};
    pairing_->receiver = serial;
    pairing_->before = before;
    pairing_->sawEmpty = before.empty();
  }
  Pairing& p = *pairing_;
  if (!again || !address.empty()) p.returnAddress = address;
  p.haveMessageId = message.contains("message_id") && message.at("message_id").is_number();
  if (p.haveMessageId) p.messageId = message.at("message_id").get<double>();
  const auto status = message.find("sendStatusUpdates");
  p.sendStatus = status != message.end() && status->is_boolean() && status->get<bool>();
  p.deadline = Now() + timeout;
  if (!session.SetPairing(serial, timeout)) {
    Finish(session, "failed_to_start", "", expected);
    return;
  }
  if (log_) log_("pairing: receiver " + serial + (again ? " still" : "") + " in pairing mode for " +
                 std::to_string(int(timeout)) + " s");
  if (p.sendStatus) Reply(address, Discover(session, expected, nullptr));
}

void PairingMailbox::Poll(Session& session, const std::string& expected) {
  // A broken mailbox disables input, not an already-running pairing's outcome.
  for (int guard = 0; mailbox_ && guard < 16; ++guard) {
    std::string text;
    if (!Read(text)) break;
    const json message = pairing_input::Parse(text);
    if (!message.is_object()) continue;
    const std::string type = Text(message, "type"), address = Text(message, "returnAddress");
    const std::string id = message.contains("message_id") ? message.at("message_id").dump() : "";
    const std::string* idp = id.empty() ? nullptr : &id;
    if (log_) log_("pairing: " + (type.empty() ? std::string("message without a type") : type) + " from " +
                   (address.empty() ? std::string("(no return address)") : address));
    if (type == "discover_dongles") {
      const std::string reply = Discover(session, expected, idp);
      if (log_) log_("pairing: dongles " + reply);
      Reply(address, reply);
    } else if (type == "start_pairing") {
      StartPairing(session, message, expected);
    } else if (type == "stop_pairing") {
      if (pairing_ && Text(message, "serial") == pairing_->receiver) {
        session.SetPairing(pairing_->receiver, 0);
        if (!address.empty()) pairing_->returnAddress = address;
        Finish(session, "canceled", "", expected);
      }
    }
  }
  if (!pairing_) return;
  // A device on the receiver: paired (one connected before must drop off first).
  for (const auto& r : session.Receivers()) {
    if (r.serial != pairing_->receiver) continue;
    if (r.connected.empty()) pairing_->sawEmpty = true;
    else if (pairing_->sawEmpty || r.connected != pairing_->before) {
      Finish(session, "success", r.connected, expected);
      return;
    }
  }
  if (Now() > pairing_->deadline) Finish(session, "timeout", "", expected);
}

}  // namespace lh::openvr
