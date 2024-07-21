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
set(Account const& account);

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

/** Sets the optional PublicKey on a JTx. */
class pk
{
private:
    PublicKey pk_;

public:
    explicit pk(PublicKey const& pk) : pk_(pk)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Set the optional Signature on a JTx */
class sig
{
private:
    Buffer sig_;

public:
    explicit sig(Buffer const& sig) : sig_(sig)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

}  // namespace firewall
}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
