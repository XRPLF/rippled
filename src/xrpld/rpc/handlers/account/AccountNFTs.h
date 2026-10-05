#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STObject.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/account_nfts/Types.hpp>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace xrpl::rpc {

class AccountNFTsHandler : public HandlerFor<::rpc::spec::handlers::account_nfts::Input>
{
public:
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;
        AccountID account;
        std::uint32_t limit = 0;
        std::vector<STObject> nfts;
        std::optional<uint256> marker;
    };

    explicit AccountNFTsHandler(JsonContext&);

    [[nodiscard]] std::expected<Output, ::rpc::Status>
    process(Input const& input) const;

    void
    writeResult(json::Value& value, Output const& output) const;

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
