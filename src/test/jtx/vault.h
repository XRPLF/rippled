#ifndef XRPL_TEST_JTX_VAULT_H_INCLUDED
#define XRPL_TEST_JTX_VAULT_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/amount.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Keylet.h>

#include <optional>
#include <tuple>

namespace ripple {
namespace test {
namespace jtx {

class Env;

struct Vault
{
    Env& env;

    struct CreateArgs
    {
        Account owner;
        Asset asset;
        std::optional<std::uint32_t> flags{};
    };

    /** Return a VaultCreate transaction and the Vault's expected keylet. */
    std::tuple<Json::Value, Keylet>
    create(CreateArgs const& args);

    struct SetArgs
    {
        Account owner;
        uint256 id;
    };

    Json::Value
    set(SetArgs const& args);

    struct DeleteArgs
    {
        Account owner;
        uint256 id;
    };

    Json::Value
    del(DeleteArgs const& args);

    struct DepositArgs
    {
        Account depositor;
        uint256 id;
        STAmount amount;
    };

    Json::Value
    deposit(DepositArgs const& args);

    struct WithdrawArgs
    {
        Account depositor;
        uint256 id;
        STAmount amount;
    };

    Json::Value
    withdraw(WithdrawArgs const& args);

    struct ClawbackArgs
    {
        Account issuer;
        uint256 id;
        Account holder;
        std::optional<STAmount> amount{};
    };

    Json::Value
    clawback(ClawbackArgs const& args);
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
