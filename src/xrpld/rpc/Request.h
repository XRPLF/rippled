#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Fees.h>

namespace xrpl {

class Application;

namespace RPC {

struct Request
{
    explicit Request(
        beast::Journal journal,
        std::string const& method,
        Json::Value& params,
        Application& app)
        : journal(journal), method(method), params(params), fee(Resource::feeReferenceRPC), app(app)
    {
    }

    // [in] The Journal for logging
    beast::Journal const journal;

    // [in] The JSON-RPC method
    std::string method;

    // [in] The Ripple-specific "params" object
    Json::Value params;

    // [in, out] The resource cost for the command
    Resource::Charge fee;

    // [out] The JSON-RPC response
    Json::Value result;

    // [in] The Application instance
    Application& app;

private:
    Request&
    operator=(Request const&);
};

}  // namespace RPC
}  // namespace xrpl
