#ifndef XRPL_RPC_REQUEST_H_INCLUDED
#define XRPL_RPC_REQUEST_H_INCLUDED

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Fees.h>

namespace ripple {

class Application;

namespace RPC {

struct Request
{
    explicit Request(
        beast::Journal journal_,
        std::string const& method_,
        Json::Value& params_,
        Application& app_)
        : journal(journal_)
        , method(method_)
        , params(params_)
        , fee(Resource::feeReferenceRPC)
        , app(app_)
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
}  // namespace ripple

#endif
