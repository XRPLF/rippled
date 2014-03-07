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

namespace ripple {
namespace PeerFinder {

Config::Config ()
    : maxPeers (Tuning::defaultMaxPeers)
    , outPeers (calcOutPeers ())
    , wantIncoming (true)
    , autoConnect (true)
    , listeningPort (0)
{
}

double Config::calcOutPeers () const
{
    return std::max (
        maxPeers * Tuning::outPercent * 0.01,
            double (Tuning::minOutCount));
}

void Config::applyTuning ()
{
    if (maxPeers < Tuning::minOutCount)
        maxPeers = Tuning::minOutCount;
    outPeers = calcOutPeers ();
}

void Config::onWrite (beast::PropertyStream::Map &map)
{
    map ["max_peers"]       = maxPeers;
    map ["out_peers"]       = outPeers;
    map ["want_incoming"]   = wantIncoming;
    map ["auto_connect"]    = autoConnect;
    map ["port"]            = listeningPort;
    map ["features"]        = features;
}

}
}
