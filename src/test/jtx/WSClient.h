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
    /** Retrieve a message. */
    virtual std::optional<json::Value>
    getMsg(std::chrono::milliseconds const& timeout = std::chrono::milliseconds{0}) = 0;

    /** Retrieve a message that meets the predicate criteria. */
    virtual std::optional<json::Value>
    findMsg(
        std::chrono::milliseconds const& timeout,
        std::function<bool(json::Value const&)> pred) = 0;
};

/** Returns a client operating through WebSockets/S. */
std::unique_ptr<WSClient>
makeWSClient(
    Config const& cfg,
    bool v2 = true,
    unsigned rpcVersion = 2,
    std::unordered_map<std::string, std::string> const& headers = {});

}  // namespace xrpl::test
