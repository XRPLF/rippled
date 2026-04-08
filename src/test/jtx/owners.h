#pragma once

#include <test/jtx/Env.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>

namespace xrpl {

namespace detail {

std::uint32_t
owned_count_of(ReadView const& view, AccountID const& id, LedgerEntryType type);

void
owned_count_helper(
    test::jtx::Env& env,
    AccountID const& id,
    LedgerEntryType type,
    std::uint32_t value);

}  // namespace detail

namespace test {
namespace jtx {

// Helper for aliases
template <LedgerEntryType Type>
class owner_count
{
private:
    Account account_;
    std::uint32_t value_;

public:
    owner_count(Account const& account, std::uint32_t value) : account_(account), value_(value)
    {
    }

    void
    operator()(Env& env) const
    {
        xrpl::detail::owned_count_helper(env, account_.id(), Type, value_);
    }
};

/** Match the number of items in the account's owner directory */
class owners
{
private:
    Account account_;
    std::uint32_t value_;

public:
    owners(Account const& account, std::uint32_t value) : account_(account), value_(value)
    {
    }

    void
    operator()(Env& env) const;
};

/** Match the number of trust lines in the account's owner directory */
using lines = owner_count<ltRIPPLE_STATE>;

/** Match the number of offers in the account's owner directory */
using offers = owner_count<ltOFFER>;

/** Match the number of MPToken in the account's owner directory */
using mptokens = owner_count<ltMPTOKEN>;

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
