#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

#include <boost/program_options.hpp>

#include <optional>
#include <string>
#include <utility>

namespace xrpl {

/**
 * This server's identity, resolved before the Application exists.
 *
 * Telemetry stamps the node public key into resource attributes that are
 * immutable once built, and those resources are built in ApplicationImp's
 * member-init list. So the identity has to be decided before construction,
 * from the config and the command line alone.
 *
 * Always returns a keypair. It derives one from a configured seed, else reads
 * the wallet database if it already exists, else mints one. Nothing is created
 * or written here: getNodeIdentity() persists the result once setup() has
 * opened the database.
 *
 * @param config  The server configuration.
 * @param cmdline The command line parameters passed into the application.
 * @param journal Journal for reporting an unreadable database.
 * @return This node's keypair.
 * @throws std::runtime_error if a configured seed is malformed.
 */
std::pair<PublicKey, SecretKey>
resolveNodeIdentity(
    Config const& config,
    boost::program_options::variables_map const& cmdline,
    beast::Journal journal);

/**
 * The cryptographic credentials identifying this server instance, persisted.
 *
 * Called from setup(), once the wallet database is open. Stores @p resolved
 * when the database holds no identity, and returns whatever the database holds
 * when it does.
 *
 * @param app The application object
 * @param cmdline The command line parameters passed into the application.
 * @param resolved The keypair resolveNodeIdentity() decided before
 *        construction, which telemetry is already reporting.
 * @return This node's keypair.
 */
std::pair<PublicKey, SecretKey>
getNodeIdentity(
    Application& app,
    boost::program_options::variables_map const& cmdline,
    std::pair<PublicKey, SecretKey> const& resolved);

}  // namespace xrpl
