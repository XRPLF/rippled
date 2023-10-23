//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_DID_H_INCLUDED
#define RIPPLE_TEST_JTX_DID_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/owners.h>

namespace ripple {
namespace test {
namespace jtx {

/** DID operations. */
namespace did {

Json::Value
set(jtx::Account const& account);

Json::Value
setValid(jtx::Account const& account);

/** Sets the optional DIDDocument on a DIDSet. */
class document
{
private:
    std::string document_;

public:
    explicit document(std::string const& u) : document_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfDIDDocument.jsonName] = document_;
    }
};

/** Sets the optional URI on a DIDSet. */
class uri
{
private:
    std::string uri_;

public:
    explicit uri(std::string const& u) : uri_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfURI.jsonName] = uri_;
    }
};

/** Sets the optional Attestation on a DIDSet. */
class data
{
private:
    std::string data_;

public:
    explicit data(std::string const& u) : data_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfData.jsonName] = data_;
    }
};

Json::Value
del(jtx::Account const& account);

}  // namespace did

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
