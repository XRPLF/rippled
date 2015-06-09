//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_MULTISIGN_H_INCLUDED
#define RIPPLE_TEST_JTX_MULTISIGN_H_INCLUDED

#include <ripple/test/jtx/Account.h>
#include <ripple/test/jtx/amounts.h>
#include <ripple/test/jtx/owners.h>
#include <ripple/test/jtx/tags.h>
#include <cstdint>

namespace ripple {
namespace test {
namespace jtx {

/** A signer in a SignerList */
struct signer
{
    std::uint32_t weight;
    Account account;

    signer (Account account_,
            std::uint32_t weight_ = 1)
        : weight(weight_)
        , account(std::move(account_))
    {
    }
};

Json::Value
signers (Account const& account,
    std::uint32_t quorum,
        std::vector<signer> const& v);

/** Remove a signer list. */
Json::Value
signers (Account const& account, none_t);

//------------------------------------------------------------------------------

/** Set a multisignature on a JTx. */
class msig
{
private:
    std::vector<Account> accounts_;

public:
    msig (std::vector<Account> accounts)
        : accounts_(std::move(accounts))
    {
    }

    template <class AccountType, class... Accounts>
    msig (AccountType&& a0, Accounts&&... aN)
        : msig(make_vector(
            std::forward<AccountType>(a0),
                std::forward<Accounts>(aN)...))
    {
    }

    void
    operator()(Env const&, JTx& jt) const;

private:
    template <class AccountType>
    static
    void
    helper (std::vector<Account>& v,
        AccountType&& account)
    {
        v.emplace_back(std::forward<
            Account>(account));
    }

    template <class AccountType, class... Accounts>
    static
    void
    helper (std::vector<Account>& v,
        AccountType&& a0, Accounts&&... aN)
    {
        helper(v, std::forward<AccountType>(a0));
        helper(v, std::forward<Accounts>(aN)...);
    }

    template <class... Accounts>
    static
    std::vector<Account>
    make_vector(Accounts&&... accounts)
    {
        std::vector<Account> v;
        v.reserve(sizeof...(accounts));
        helper(v, std::forward<
            Accounts>(accounts)...);
        return v;
    }
};

//------------------------------------------------------------------------------

/** Set a multisignature on a JTx. */
class msig2_t
{
private:
    std::map<Account,
        std::set<Account>> sigs_;

public:
    msig2_t (std::vector<std::pair<
        Account, Account>> sigs);

    void
    operator()(Env const&, JTx& jt) const;
};

inline
msig2_t
msig2 (std::vector<std::pair<
    Account, Account>> sigs)
{
    return msig2_t(std::move(sigs));
}

//------------------------------------------------------------------------------

/** The number of signer lists matches. */
using siglists = owner_count<ltSIGNER_LIST>;

} // jtx
} // test
} // ripple

#endif
