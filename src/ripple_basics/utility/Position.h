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

#ifndef RIPPLE_UTILITY_POSITION_H
#define RIPPLE_UTILITY_POSITION_H

namespace ripple {

enum Position { FIRST, PREVIOUS, CURRENT, NEXT, LAST };

template <typename Container>
class ContainerPosition
{
  public:
    typedef typename Container::value_type value_type;

    explicit ContainerPosition (Container& collection, unsigned int index = 0)
        : collection_(collection), index_(index)
    {}

    unsigned int index (Position position = CURRENT) const
    {
        switch (position) {
          case FIRST:     return 0;
          case PREVIOUS:  return index_ ? index_ - 1 : 0;
          case CURRENT:   return index_;
          default:        break;
        }

        unsigned int size = collection_.size ();
        auto last = size ? size - 1 : 0;
        switch (position) {
          case NEXT:      return std::min(index_ + 1, last);
          case LAST:      return last;
          default:        bassert(false);
        }
    }

    bool isFirst() const { return !index_; }
    bool isLast() const { return index_ == index(LAST); }

    value_type& at (Position position = CURRENT)
    {
        return collection_.at(index(position));
    }

    const value_type& at (Position position = CURRENT) const
    {
        return collection_.at(index(position));
    }

    ContainerPosition<Container> move(Position position) const
    {
        return ContainerPosition<Container>(collection_, index(position));
    }

  private:
    Container& collection_;
    unsigned int const index_;
};

} // ripple

#endif
