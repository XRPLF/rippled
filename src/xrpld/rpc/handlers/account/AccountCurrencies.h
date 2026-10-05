#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/UintTypes.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/account_currencies/Types.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <set>

namespace xrpl::rpc {

class AccountCurrenciesHandler : public HandlerFor<::rpc::spec::handlers::account_currencies::Input>
{
public:
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;
        std::optional<ErrorCodeI> error;
        std::set<Currency> send;
        std::set<Currency> receive;
    };

    explicit AccountCurrenciesHandler(JsonContext&);

    [[nodiscard]] std::expected<Output, ::rpc::Status>
    process(Input const& input) const;

    void
    writeResult(json::Value& value, Output const& output) const;

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
