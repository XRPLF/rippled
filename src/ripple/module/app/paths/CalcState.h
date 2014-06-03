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

#ifndef RIPPLE_CALCSTATE_H
#define RIPPLE_CALCSTATE_H

namespace ripple {

typedef TER ErrorCode;

class CalcState {
  public:
    CalcState(
        unsigned int nodeIndex, PathState& state, LedgerEntrySet& ledger, bool quality)
            : nodeIndex_(nodeIndex),
              pathState_(state),
              ledger_(ledger),
              quality_(quality)
    {}

    enum Direction { BACKWARD, FORWARD };
    TER calc(Direction);
    TER calcAccount(Direction);
    TER calcOffer(Direction);
    TER calcDeliver(Direction);
    TER calcAdvance(Direction);
    void nextPath(LedgerEntrySet const& checkpoint) const;

  private:
    enum NodeCursor { FIRST, PREVIOUS, CURRENT, NEXT, LAST };

    unsigned int index(NodeCursor cursor = CURRENT) const
    {
        switch (cursor) {
          case FIRST:
            return 0;
          case PREVIOUS:
            return nodeIndex_ ? nodeIndex_ - 1 : 0;
          case CURRENT:
            return nodeIndex_;
          default:
            break;
        }
        unsigned int size = pathState_.vpnNodes.size ();
        auto last = size ? size - 1 : 0;
        switch (cursor) {
          case NEXT:
              return std::min(nodeIndex_ + 1, last);
          case LAST:
            return last;
          default:
            bassert(false);
        }
    }

    PathState::Node& node(NodeCursor cursor = CURRENT)
    {
        return pathState_.vpnNodes[index(cursor)];
    }

    CalcState state(NodeCursor cursor = CURRENT)
    {
        return CalcState(index(cursor), pathState_, ledger_, quality_);
    }

    LedgerEntrySet& ledger()
    {
        return ledger_;
    }

    bool quality() const
    {
        return quality_;
    }

  private:
    unsigned int const nodeIndex_;
    PathState& pathState_;
    LedgerEntrySet& ledger_;
    bool const quality_;
};

inline bool isAccount(PathState::Node const& node)
{
    return is_bit_set (node.uFlags, STPathElement::typeAccount);
}

inline STAmount copyCurrencyAndIssuer(const STAmount& a)
{
    return STAmount(a.getCurrency(), a.getIssuer());
}

} // ripple

#endif
