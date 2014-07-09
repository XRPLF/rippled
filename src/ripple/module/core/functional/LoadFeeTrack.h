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

#ifndef RIPPLE_LOADFEETRACK_H_INCLUDED
#define RIPPLE_LOADFEETRACK_H_INCLUDED

namespace ripple {

/** Manages the current fee schedule.

    The "base" fee is the cost to send a reference transaction under no load,
    expressed in millionths of one XRP.

    The "load" fee is how much the local server currently charges to send a
    reference transaction. This fee fluctuates based on the load of the
    server.
*/
// VFALCO TODO Rename "load" to "current".
class LoadFeeTrack
{
public:
    /** Create a new tracker.
    */
    static LoadFeeTrack* New (beast::Journal journal);

    virtual ~LoadFeeTrack () { }

    // Scale from fee units to millionths of a ripple
    virtual uint64 scaleFeeBase (uint64 fee, uint64 baseFee,
                                        uint32 referenceFeeUnits) = 0;

    // Scale using load as well as base rate
    virtual uint64 scaleFeeLoad (uint64 fee, uint64 baseFee,
                                        uint32 referenceFeeUnits,
                                        bool bAdmin) = 0;

    virtual void setRemoteFee (uint32) = 0;

    virtual uint32 getRemoteFee () = 0;
    virtual uint32 getLocalFee () = 0;
    virtual uint32 getClusterFee () = 0;

    virtual uint32 getLoadBase () = 0;
    virtual uint32 getLoadFactor () = 0;

    virtual Json::Value getJson (uint64 baseFee, uint32 referenceFeeUnits) = 0;

    virtual void setClusterFee (uint32) = 0;
    virtual bool raiseLocalFee () = 0;
    virtual bool lowerLocalFee () = 0;
    virtual bool isLoadedLocal () = 0;
    virtual bool isLoadedCluster () = 0;
};

} // ripple

#endif
