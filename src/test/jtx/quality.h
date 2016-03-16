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

#ifndef RIPPLE_TEST_JTX_QUALITY_H_INCLUDED
#define RIPPLE_TEST_JTX_QUALITY_H_INCLUDED

#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

/** Sets the literal QualityIn on a trust JTx. */
class qualityIn
{
private:
    std::uint32_t qIn_;

public:
    explicit qualityIn (std::uint32_t qIn)
    : qIn_ (qIn)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the QualityIn on a trust JTx. */
class qualityInPercent
{
private:
    std::uint32_t qIn_;

public:
    explicit qualityInPercent (double percent);

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the literal QualityOut on a trust JTx. */
class qualityOut
{
private:
    std::uint32_t qOut_;

public:
    explicit qualityOut (std::uint32_t qOut)
    : qOut_ (qOut)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the QualityOut on a trust JTx as a percentage. */
class qualityOutPercent
{
private:
    std::uint32_t qOut_;

public:
    explicit qualityOutPercent (double percent);

    void
    operator()(Env&, JTx& jtx) const;
};

} // jtx
} // test
} // ripple

#endif
