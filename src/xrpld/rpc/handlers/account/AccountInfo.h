#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/jss.h>

#include <array>

namespace xrpl::RPC {

class AccountInfoHandler
{
public:
    explicit AccountInfoHandler(JsonContext&);

    Status
    check();

    void
    writeResult(json::Value&);

    // NOLINTBEGIN(readability-identifier-naming)
    static constexpr char name[] = "account_info";

    static constexpr unsigned minApiVer = RPC::kAPI_MINIMUM_SUPPORTED_VERSION;

    static constexpr unsigned maxApiVer = RPC::kAPI_MAXIMUM_VALID_VERSION;

    static constexpr Role role = Role::USER;

    static constexpr Condition condition = Condition::NoCondition;

    static constexpr std::array requestFields = {
        FieldSpec{jss::account, FieldRequirement::Optional, json::ValueType::String},
        FieldSpec{jss::ident, FieldRequirement::Optional, json::ValueType::String},
        FieldSpec{jss::queue, FieldRequirement::Optional, json::ValueType::Boolean},
    };
    // NOLINTEND(readability-identifier-naming)

private:
    JsonContext& context_;
};

}  // namespace xrpl::RPC
