#pragma once

#include <xrpld/app/main/Application.h>  // IWYU pragma: keep
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/MethodNames.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>

#include <rpcspec/Errors.hpp>

#include <cstdint>
#include <expected>
#include <functional>
#include <string_view>

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

    // NOLINTBEGIN(readability-identifier-naming)
    static constexpr std::string_view name = method::kVersion;

    static constexpr unsigned minApiVer = rpc::kApiMinimumSupportedVersion;

    static constexpr unsigned maxApiVer = rpc::kApiMaximumValidVersion;

    static constexpr Role role = Role::USER;

    static constexpr Condition condition = Condition::NoCondition;
    // NOLINTEND(readability-identifier-naming)

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
