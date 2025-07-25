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

#include <test/jtx/Account.h>
#include <test/jtx/SignerUtils.h>
#include <test/jtx/amount.h>
#include <test/jtx/owners.h>
#include <test/jtx/tags.h>

#include <concepts>
#include <cstdint>
#include <optional>

namespace ripple {
namespace test {
namespace jtx {

/** A signer in a SignerList */
struct signer
{
    std::uint32_t weight;
    Account account;
    std::optional<uint256> tag;

    signer(
        Account account_,
        std::uint32_t weight_ = 1,
        std::optional<uint256> tag_ = std::nullopt)
        : weight(weight_), account(std::move(account_)), tag(std::move(tag_))
    {
    }
};

Json::Value
signers(
    Account const& account,
    std::uint32_t quorum,
    std::vector<signer> const& v);

/** Remove a signer list. */
Json::Value
signers(Account const& account, none_t);

//------------------------------------------------------------------------------

/** Set a multisignature on a JTx. */
class msig
{
public:
    std::vector<Reg> signers;

    msig(std::vector<Reg> signers_) : signers(std::move(signers_))
    {
        sortSigners(signers);
    }

    template <class AccountType, class... Accounts>
        requires std::convertible_to<AccountType, Reg>
    explicit msig(AccountType&& a0, Accounts&&... aN)
        : signers{std::forward<AccountType>(a0), std::forward<Accounts>(aN)...}
    {
        sortSigners(signers);
    }

    void
    operator()(Env&, JTx& jt) const;
};

//------------------------------------------------------------------------------

/** The number of signer lists matches. */
using siglists = owner_count<ltSIGNER_LIST>;

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
