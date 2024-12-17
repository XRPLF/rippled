//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Transia, LLC.

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
#include <xrpl/protocol/STAmount.h>

namespace ripple {
namespace test {
namespace jtx {
namespace firewall {

/** Set/Update a firewall. */
Json::Value
set(Account const& account, std::uint32_t const& seq, STAmount const& fee);

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

/** Sets the optional Amount on a JTx. */
class amt
{
private:
    STAmount amt_;

public:
    explicit amt(STAmount const& amt) : amt_(amt)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional Issuer on a JTx. */
class issuer
{
private:
    jtx::Account issuer_;

public:
    explicit issuer(jtx::Account const& issuer) : issuer_(issuer)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional Authorize on a JTx. */
class auth
{
private:
    jtx::Account auth_;

public:
    explicit auth(jtx::Account const& auth) : auth_(auth)
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
