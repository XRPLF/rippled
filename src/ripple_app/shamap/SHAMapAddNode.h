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

// results of adding nodes
class SHAMapAddNode
{
public:
    SHAMapAddNode ()
        : mInvalid (false)
        , mUseful (false)
    {
    }

    void setInvalid ()
    {
        mInvalid = true;
    }
    void setUseful ()
    {
        mUseful = true;
    }
    void reset ()
    {
        mInvalid = false;
        mUseful = false;
    }

    bool isInvalid () const
    {
        return mInvalid;
    }
    bool isUseful () const
    {
        return mUseful;
    }

    bool combine (SHAMapAddNode const& n)
    {
        // VFALCO NOTE What is the meaning of these lines?

        if (n.mInvalid)
        {
            mInvalid = true;
            return false;
        }

        if (n.mUseful)
            mUseful = true;

        return true;
    }

    operator bool () const
    {
        return !mInvalid;
    }

    static SHAMapAddNode okay ()
    {
        return SHAMapAddNode (false, false);
    }
    static SHAMapAddNode useful ()
    {
        return SHAMapAddNode (false, true);
    }
    static SHAMapAddNode invalid ()
    {
        return SHAMapAddNode (true, false);
    }

private:
    SHAMapAddNode (bool i, bool u)
        : mInvalid (i)
        , mUseful (u)
    {
    }

    bool mInvalid;
    bool mUseful;
};

#endif
