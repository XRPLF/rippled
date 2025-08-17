//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_TEST_JTX_FIREWALL_H_INCLUDED
#define RIPPLE_TEST_JTX_FIREWALL_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Firewall.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STData.h>

namespace ripple {
namespace test {
namespace jtx {
namespace firewall {

XRPAmount
calcFirewallFee(test::jtx::Env const& env, uint32_t const& numSigners);

/** Set a firewall. */
Json::Value
set(Account const& account);

/** Update a firewall. */
Json::Value
set(Account const& account,
    uint256 const& firewallID,
    uint32_t seq,
    STAmount const& fee);

/** Adds a new Incoming Firewall Rule on a JTx and autofills. */
template <typename T>
class rule
{
private:
    std::uint16_t leType_;
    SField const& field_;
    FirewallOperator op_;
    T value_;
    std::optional<std::uint32_t> timePeriod_;

public:
    rule(
        std::uint16_t const& leType,
        SField const& field,
        FirewallOperator const& op,
        T const& value,
        std::optional<std::uint32_t> const& timePeriod = std::nullopt)
        : leType_(leType)
        , field_(field)
        , op_(op)
        , value_(value)
        , timePeriod_(timePeriod)
    {
    }

    void
    operator()(Env& env, JTx& jt) const
    {
        auto const index = jt.jv[jss::FirewallRules].size();
        Json::Value& rule = jt.jv[jss::FirewallRules][index];

        // Initialize the firewall rule
        rule = Json::Value{};
        rule[jss::FirewallRule][sfLedgerEntryType.jsonName] = leType_;
        rule[jss::FirewallRule][sfFieldCode.jsonName] = field_.fieldCode;
        rule[jss::FirewallRule][sfComparisonOperator.jsonName] =
            static_cast<std::uint16_t>(op_);
        rule[jss::FirewallRule][sfFirewallValue][jss::type] =
            getTypeString(field_.fieldType);
        rule[jss::FirewallRule][sfFirewallValue][jss::value] = value_;
        if (timePeriod_.has_value())
            rule[jss::FirewallRule][sfTimePeriod.jsonName] = *timePeriod_;
    }
};

/** Sets the optional TimePeriod on a JTx. */
class time_period
{
private:
    std::uint32_t value_;

public:
    explicit time_period(std::uint32_t const& value) : value_(value)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional sfCounterParty on a JTx. */
class counter_party
{
private:
    jtx::Account counterParty_;

public:
    explicit counter_party(jtx::Account const& counterParty)
        : counterParty_(counterParty)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional sfBackup on a JTx. */
class backup
{
private:
    jtx::Account backup_;

public:
    explicit backup(jtx::Account const& backup) : backup_(backup)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Set a firewall signature on a JTx. */
class sig
{
public:
    struct Reg
    {
        Account acct;
        Account sig;

        Reg(Account const& masterSig) : acct(masterSig), sig(masterSig)
        {
        }

        Reg(Account const& acct_, Account const& regularSig)
            : acct(acct_), sig(regularSig)
        {
        }

        Reg(char const* masterSig) : acct(masterSig), sig(masterSig)
        {
        }

        Reg(char const* acct_, char const* regularSig)
            : acct(acct_), sig(regularSig)
        {
        }

        bool
        operator<(Reg const& rhs) const
        {
            return acct < rhs.acct;
        }
    };

    std::vector<Reg> signers;

public:
    sig(std::vector<Reg> signers_);

    template <class AccountType, class... Accounts>
        requires std::convertible_to<AccountType, Reg>
    explicit sig(AccountType&& a0, Accounts&&... aN)
        : sig{std::vector<Reg>{
              std::forward<AccountType>(a0),
              std::forward<Accounts>(aN)...}}
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

/** Set a firewall multi signature on a JTx. */
class msig
{
public:
    struct Reg
    {
        Account acct;
        Account sig;

        Reg(Account const& masterSig) : acct(masterSig), sig(masterSig)
        {
        }

        Reg(Account const& acct_, Account const& regularSig)
            : acct(acct_), sig(regularSig)
        {
        }

        Reg(char const* masterSig) : acct(masterSig), sig(masterSig)
        {
        }

        Reg(char const* acct_, char const* regularSig)
            : acct(acct_), sig(regularSig)
        {
        }

        bool
        operator<(Reg const& rhs) const
        {
            return acct < rhs.acct;
        }
    };

    Account master;  // Add a member to hold the master account
    std::vector<Reg> signers;

public:
    msig(Account const& masterAccount, std::vector<Reg> signers_);

    template <class AccountType, class... Accounts>
        requires std::convertible_to<AccountType, Reg>
    explicit msig(
        Account const& masterAccount,
        AccountType&& a0,
        Accounts&&... aN)
        : master(masterAccount)
        ,  // Initialize master account
        signers{std::vector<Reg>{
            std::forward<AccountType>(a0),
            std::forward<Accounts>(aN)...}}
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace firewall
}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
