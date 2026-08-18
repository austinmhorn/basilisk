#pragma once

#include <memory>

#include "ClientSessionController.hpp"

namespace basilisk::game::demo {

// Development-only session data and command sinks. Demo snapshots are still
// ingested through the same controller API a future transport will use.
[[nodiscard]] std::unique_ptr<ClientSessionController>
makeDemoSessionController();

} // namespace basilisk::game::demo
