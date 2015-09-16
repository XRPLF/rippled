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

#ifndef RIPPLE_APP_MAIN_LOCALCREDENTIALS_H_INCLUDED
#define RIPPLE_APP_MAIN_LOCALCREDENTIALS_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/protocol/RippleAddress.h>
#include <mutex>
#include <string>

namespace ripple {

/** Holds the cryptographic credentials identifying this instance of the server. */
class LocalCredentials
{
public:
    LocalCredentials (Application& app);
    LocalCredentials (LocalCredentials const&) = delete;
    LocalCredentials& operator= (LocalCredentials const&) = delete;

    // Begin processing.
    // - Maintain peer connectivity through validation and peer management.
    void start ();

    RippleAddress const& getNodePublic () const
    {
        return mNodePublicKey;
    }

    RippleAddress const& getNodePrivate () const
    {
        return mNodePrivateKey;
    }

private:
    bool nodeIdentityLoad ();
    bool nodeIdentityCreate ();

private:
    Application& app_;
    std::recursive_mutex mLock;

    RippleAddress mNodePublicKey;
    RippleAddress mNodePrivateKey;
};

} // ripple

#endif
