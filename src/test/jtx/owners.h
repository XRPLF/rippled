#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <cstdint>
#include <utility>

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

namespace test::jtx {

// Helper for aliases
template <LedgerEntryType Type>
class OwnerCount
{
private:
    Account account_;
    std::uint32_t value_;

public:
    OwnerCount(Account account, std::uint32_t value) : account_(std::move(account)), value_(value)
    {
    }

    void
    operator()(Env& env) const
    {
        xrpl::detail::ownedCountHelper(env, account_.id(), Type, value_);
    }
};

/**
 * Match the number of items in the account's owner directory
 */
class Owners
{
private:
    Account account_;
    std::uint32_t value_;

public:
    Owners(Account account, std::uint32_t value) : account_(std::move(account)), value_(value)
    {
    }

    void
    operator()(Env& env) const;
};

/**
 * Match the account's SponsoredOwnerCount field: the number of owned
 * objects whose reserve is sponsored by another account.
 */
class SponsoredOwners
{
private:
    Account account_;
    std::uint32_t value_;

public:
    SponsoredOwners(Account account, std::uint32_t value)
        : account_(std::move(account)), value_(value)
    {
    }

    void
    operator()(Env& env) const;
};

/**
 * Match the account's SponsoringOwnerCount field: the number of objects
 * (owned by other accounts) whose reserve this account sponsors.
 */
class SponsoringOwners
{
private:
    Account account_;
    std::uint32_t value_;

public:
    SponsoringOwners(Account account, std::uint32_t value)
        : account_(std::move(account)), value_(value)
    {
    }

    void
    operator()(Env& env) const;
};

/**
 * Match the account's SponsoringAccountCount field: the number of accounts
 * whose base reserve this account sponsors.
 */
class SponsoringAccountCount
{
private:
    Account account_;
    std::uint32_t value_;

public:
    SponsoringAccountCount(Account account, std::uint32_t value)
        : account_(std::move(account)), value_(value)
    {
    }

    void
    operator()(Env& env) const;
};

/**
 * Match the number of trust lines in the account's owner directory
 */
using lines = OwnerCount<ltRIPPLE_STATE>;

/**
 * Match the number of offers in the account's owner directory
 */
using offers = OwnerCount<ltOFFER>;

/**
 * Match the number of MPToken in the account's owner directory
 */
using mptokens = OwnerCount<ltMPTOKEN>;

}  // namespace test::jtx

}  // namespace xrpl
