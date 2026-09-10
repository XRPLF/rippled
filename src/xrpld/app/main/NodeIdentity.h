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
 * The cryptographic credentials identifying this server instance.
 *
 * @param app The application object
 * @param cmdline The command line parameters passed into the application.
 */
std::pair<PublicKey, SecretKey>
getNodeIdentity(Application& app, boost::program_options::variables_map const& cmdline);

/**
 * This server's public key, read without creating or modifying anything.
 *
 * For callers that need the identity before the Application exists, such as
 * telemetry building its resource attributes in the member-init list. Derives
 * from a configured seed when there is one, otherwise reads the wallet database
 * only if it already exists.
 *
 * getNodeIdentity() remains authoritative and mints a key when none exists.
 *
 * @param config  The server configuration.
 * @param cmdline The command line parameters passed into the application.
 * @param journal Journal for reporting an unreadable database.
 * @return The base58-encoded node public key, or std::nullopt if none can be
 *         read.
 */
std::optional<std::string>
resolveNodePublicKey(
    Config const& config,
    boost::program_options::variables_map const& cmdline,
    beast::Journal journal);

}  // namespace xrpl
