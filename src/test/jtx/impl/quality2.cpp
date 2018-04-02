//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <test/jtx/quality.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/Quality.h>

namespace ripple {
namespace test {
namespace jtx {

qualityInPercent::qualityInPercent (double percent)
: qIn_ (static_cast<std::uint32_t>((percent / 100) * QUALITY_ONE))
{
    assert (percent <= 400 && percent >= 0);
}

qualityOutPercent::qualityOutPercent (double percent)
: qOut_ (static_cast<std::uint32_t>((percent / 100) * QUALITY_ONE))
{
    assert (percent <= 400 && percent >= 0);
}

static void
insertQualityIntoJtx (SField const& field, std::uint32_t value, JTx& jt)
{
    jt.jv[field.jsonName] = value;
}

void
qualityIn::operator()(Env&, JTx& jt) const
{
    insertQualityIntoJtx (sfQualityIn, qIn_, jt);
}

void
qualityInPercent::operator()(Env&, JTx& jt) const
{
    insertQualityIntoJtx (sfQualityIn, qIn_, jt);
}

void
qualityOut::operator()(Env&, JTx& jt) const
{
    insertQualityIntoJtx (sfQualityOut, qOut_, jt);
}

void
qualityOutPercent::operator()(Env&, JTx& jt) const
{
    insertQualityIntoJtx (sfQualityOut, qOut_, jt);
}

} // jtx
} // test
} // ripple
