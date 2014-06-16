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

#ifndef RIPPLE_SHAMAPADDNODE_H
#define RIPPLE_SHAMAPADDNODE_H

#include <beast/module/core/text/LexicalCast.h>

namespace ripple {

// results of adding nodes
class SHAMapAddNode
{
public:
    SHAMapAddNode ()
        : mGood (0)
        , mBad (0)
        , mDuplicate (0)
    {
    }

    SHAMapAddNode (int good, int bad, int duplicate)
        : mGood(good)
        , mBad(bad)
        , mDuplicate(duplicate)
    {
    }

    void incInvalid ()
    {
        ++mBad;
    }
    void incUseful ()
    {
        ++mGood;
    }
    void incDuplicate ()
    {
        ++mDuplicate;
    }

    void reset ()
    {
        mGood = mBad = mDuplicate = 0;
    }

    int getGood ()
    {
        return mGood;
    }

    bool isInvalid () const
    {
        return mBad > 0;
    }
    bool isUseful () const
    {
        return mGood > 0;
    }

    SHAMapAddNode& operator+= (SHAMapAddNode const& n)
    {
        mGood += n.mGood;
        mBad += n.mBad;
        mDuplicate += n.mDuplicate;

        return *this;
    }

    bool isGood () const
    {
        return (mGood + mDuplicate) > mBad;
    }

    static SHAMapAddNode duplicate ()
    {
        return SHAMapAddNode (0, 0, 1);
    }
    static SHAMapAddNode useful ()
    {
        return SHAMapAddNode (1, 0, 0);
    }
    static SHAMapAddNode invalid ()
    {
        return SHAMapAddNode (0, 1, 0);
    }

    std::string get ()
    {
        std::string ret;
        if (mGood > 0)
        {
            ret.append("good:");
            ret.append(beast::lexicalCastThrow<std::string>(mGood));
        }
        if (mBad > 0)
        {
            if (!ret.empty())
                ret.append(" ");
	     ret.append("bad:");
	     ret.append(beast::lexicalCastThrow<std::string>(mBad));
        }
        if (mDuplicate > 0)
        {
            if (!ret.empty())
                ret.append(" ");
	     ret.append("dupe:");
	     ret.append(beast::lexicalCastThrow<std::string>(mDuplicate));
        }
        if (ret.empty ())
            ret = "no nodes processed";
        return ret;
    }

private:

    int mGood;
    int mBad;
    int mDuplicate;
};

}

#endif
