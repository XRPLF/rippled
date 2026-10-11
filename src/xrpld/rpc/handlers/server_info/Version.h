#pragma once

#include <xrpld/app/main/Application.h>  // IWYU pragma: keep
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>

#include <rpcspec/Errors.hpp>

#include <cstdint>
#include <expected>
#include <functional>

namespace xrpl::rpc {

class VersionHandler
{
public:
    struct Output
    {
        std::uint32_t apiVersion;
        bool betaEnabled;
    };

    explicit VersionHandler(JsonContext& context) : context_(context)
    {
    }

    [[nodiscard]] std::expected<Output, ::rpc::Status>
    process() const
    {
        return Output{
            .apiVersion = context_.get().apiVersion,
            .betaEnabled = context_.get().app.config().betaRpcApi,
        };
    }

    static void
    writeResult(json::Value& obj, Output const& output)
    {
        setVersion(obj, output.apiVersion, output.betaEnabled);
    }

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
