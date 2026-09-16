#pragma once

#include <NetworkTrust.h>

namespace ota_trust {
// OtaPolicy restricts the host; the full bundle supports CDN CA rotation.
static constexpr const char* ROOT_CA = network_trust::roots;
}  // namespace ota_trust
