#include <lh/session.hpp>

int main() {
  auto session = lh::Session::Create({});
  // No Start(): this consumer must never enumerate hardware.
  if (!session->Receivers().empty() || session->HasBluetoothRadio() ||
      session->SetPairing("absent", 0) || session->SetBluetoothRadio(false) ||
      session->BluetoothRadioOn().has_value()) return 1;
  return 0;
}
