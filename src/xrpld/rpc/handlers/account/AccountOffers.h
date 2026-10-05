#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/account_offers/Types.hpp>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace xrpl::rpc {

class AccountOffersHandler : public HandlerFor<::rpc::spec::handlers::account_offers::Input>
{
public:
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;
        std::optional<ErrorCodeI> error;
        AccountID account;
        std::uint32_t limit = 0;
        std::vector<SLE::const_pointer> offers;
        std::optional<uint256> marker;
        std::uint64_t nextHint = 0;
    };

    explicit AccountOffersHandler(JsonContext&);

    [[nodiscard]] std::expected<Output, ::rpc::Status>
    process(Input const& input) const;

    void
    writeResult(json::Value& value, Output const& output) const;

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
