#ifndef XRPL_TEST_WSCLIENT_H_INCLUDED
#define XRPL_TEST_WSCLIENT_H_INCLUDED

#include <test/jtx/AbstractClient.h>

#include <xrpld/core/Config.h>

#include <chrono>
#include <memory>
#include <optional>

namespace ripple {
namespace test {

class WSClient : public AbstractClient
{
public:
    /** Retrieve a message. */
    virtual std::optional<Json::Value>
    getMsg(
        std::chrono::milliseconds const& timeout = std::chrono::milliseconds{
            0}) = 0;

    /** Retrieve a message that meets the predicate criteria. */
    virtual std::optional<Json::Value>
    findMsg(
        std::chrono::milliseconds const& timeout,
        std::function<bool(Json::Value const&)> pred) = 0;
};

/** Returns a client operating through WebSockets/S. */
std::unique_ptr<WSClient>
makeWSClient(
    Config const& cfg,
    bool v2 = true,
    unsigned rpc_version = 2,
    std::unordered_map<std::string, std::string> const& headers = {});

}  // namespace test
}  // namespace ripple

#endif
