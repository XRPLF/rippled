#pragma once

#include <test/jtx/AbstractClient.h>

#include <xrpld/core/Config.h>

#include <xrpl/json/json_value.h>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace xrpl::test {

class WSClient : public AbstractClient
{
public:
    /**
     * Retrieve a message.
     */
    virtual std::optional<json::Value>
    getMsg(std::chrono::milliseconds const& timeout = std::chrono::milliseconds{0}) = 0;

    /**
     * Retrieve a message that meets the predicate criteria.
     */
    virtual std::optional<json::Value>
    findMsg(
        std::chrono::milliseconds const& timeout,
        std::function<bool(json::Value const&)> pred) = 0;

    /**
     * Send a command and return its reply exactly as it arrived.
     *
     * `invoke` reshapes a reply into the legacy envelope so that a caller can
     * assert one shape across API versions. A test checking the envelope itself
     * needs what the server actually sent.
     *
     * At version 3 `invoke` keeps `id` and `jsonrpc` beside the legacy members,
     * as a legacy success reply does. A legacy error reply nests the whole
     * reply under `result`, `id` included, as it always has.
     *
     * @param cmd The method to call.
     * @param params The parameters to send with it.
     * @return The reply, or nullopt if none arrived.
     */
    virtual std::optional<json::Value>
    invokeRaw(std::string const& cmd, json::Value const& params) = 0;
};

/**
 * Returns a client operating through WebSockets/S.
 *
 * @param cfg The server configuration to read the port from.
 * @param v2 Whether to connect to the `ws2` port rather than `ws`.
 * @param rpcVersion The legacy `ripplerpc` envelope to select.
 * @param headers Extra headers to send with the upgrade request.
 * @param apiVersion Sent as `api_version` on every request. From version 3 it
 *        selects the JSON-RPC 2.0 envelope.
 * @return The client.
 */
std::unique_ptr<WSClient>
makeWSClient(
    Config const& cfg,
    bool v2 = true,
    unsigned rpcVersion = 2,
    std::unordered_map<std::string, std::string> const& headers = {},
    std::optional<unsigned> apiVersion = std::nullopt);

}  // namespace xrpl::test
