#pragma once

#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>

#include <string>
#include <string_view>

namespace xrpl::telemetry {

/**
 * Value of the `backend` label when `[node_db] type` names no registered
 * NodeStore factory.
 *
 * Startup throws on such a type when it builds the node store, before any
 * gauge is read, so a running node never exports this value.
 */
inline constexpr std::string_view kUnknownNodeStoreBackend = "unknown";

/**
 * Canonical name of the NodeStore backend that `[node_db]` selects.
 *
 * @code
 *   [node_db] type --Manager::find()--> Factory --getName()--> "NuDB"
 * @endcode
 *
 * The factory spells its own name, so `nudb` and `NuDB` give one label value.
 * The type is fixed for the life of the process: rotation copies `[node_db]`
 * and changes only the path.
 *
 * @code
 * Section nodeDb{Sections::kNodeDatabase};
 * nodeDb.set(Keys::kType, "nudb");
 * auto const name = nodeStoreBackendName(nodeDb);  // "NuDB"
 *
 * nodeDb.set(Keys::kType, "NotABackend");
 * auto const unknown = nodeStoreBackendName(nodeDb);  // kUnknownNodeStoreBackend
 * @endcode
 *
 * @note Reads only config and the factory registry, so it is safe to call
 * before the node store exists. Thread-safe: Manager::find() takes the
 * manager's own lock.
 *
 * @param nodeDb The `[node_db]` config section.
 * @return The factory's name ("NuDB", "RocksDB", "Memory" or "none"), or
 * kUnknownNodeStoreBackend when no factory matches the type.
 */
[[nodiscard]] inline std::string
nodeStoreBackendName(Section const& nodeDb)
{
    auto const* factory = node_store::Manager::instance().find(get(nodeDb, Keys::kType));
    return factory != nullptr ? factory->getName() : std::string{kUnknownNodeStoreBackend};
}

}  // namespace xrpl::telemetry
