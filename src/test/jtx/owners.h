#pragma once

#include <test/jtx/Env.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>

namespace xrpl {

namespace detail {

std::uint32_t
ownedCountOf(ReadView const& view, AccountID const& id, LedgerEntryType type);

void
ownedCountHelper(
    test::jtx::Env& env,
    AccountID const& id,
    LedgerEntryType type,
    std::uint32_t value);

}  // namespace detail

namespace test {
namespace jtx {

// Helper for aliases
template <LedgerEntryType Type>
class OwnerCount
{
private:
    Account account_;
    std::uint32_t value_;

public:
    OwnerCount(Account const& account, std::uint32_t value) : account_(account), value_(value)
    {
    }

    void
    operator()(Env& env) const
    {
        detail::ownedCountHelper(env, account_.id(), Type, value_);
    }
};

/** Match the number of items in the account's owner directory */
class Owners
{
private:
    Account account_;
    std::uint32_t value_;

public:
    Owners(Account const& account, std::uint32_t value) : account_(account), value_(value)
    {
    }

    void
    operator()(Env& env) const;
};

/** Match the number of trust lines in the account's owner directory */
using lines = OwnerCount<ltRIPPLE_STATE>;

/** Match the number of offers in the account's owner directory */
using offers = OwnerCount<ltOFFER>;

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
