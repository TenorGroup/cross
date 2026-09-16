#pragma once

#include <NetworkTrust.h>

namespace ota_trust {
// OTA stays restricted to cross.tenor.vn by OtaPolicy. Accept its RSA/ECDSA roots.
static constexpr const char* ROOT_CA = network_trust::gts;
}  // namespace ota_trust
