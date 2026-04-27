#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/amount.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Keylet.h>

#include <optional>
#include <tuple>

namespace xrpl::test::jtx {

class Env;

struct Vault
{
    Env& env;

    struct CreateArgs
    {
        Account owner;
        Asset asset;
        std::optional<std::uint32_t> flags =
            std::nullopt;  // NOLINT(readability-redundant-member-init)
    };

    /** Return a VaultCreate transaction and the Vault's expected keylet. */
    [[nodiscard]] std::tuple<Json::Value, Keylet>
    create(CreateArgs const& args) const;

    struct SetArgs
    {
        Account owner;
        uint256 id;
    };

    static Json::Value
    set(SetArgs const& args);

    struct DeleteArgs
    {
        Account owner;
        uint256 id;
    };

    static Json::Value
    del(DeleteArgs const& args);

    struct DepositArgs
    {
        Account depositor;
        uint256 id;
        STAmount amount;
    };

    static Json::Value
    deposit(DepositArgs const& args);

    struct WithdrawArgs
    {
        Account depositor;
        uint256 id;
        STAmount amount;
    };

    static Json::Value
    withdraw(WithdrawArgs const& args);

    struct ClawbackArgs
    {
        Account issuer;
        uint256 id;
        Account holder;
        std::optional<STAmount> amount = std::nullopt;  // NOLINT(readability-redundant-member-init)
    };

    static Json::Value
    clawback(ClawbackArgs const& args);
};

}  // namespace xrpl::test::jtx
