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

#ifndef RIPPLE_LOCALCREDENTIALS_H
#define RIPPLE_LOCALCREDENTIALS_H

namespace ripple {

/** Holds the cryptographic credentials identifying this instance of the server. */
class LocalCredentials : public beast::Uncopyable
{
public:
    LocalCredentials ();

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

    // Local persistence of RPC clients
    bool dataDelete (std::string const& strKey);

    // VFALCO NOTE why is strValue non-const?
    bool dataFetch (std::string const& strKey, std::string& strValue);
    bool dataStore (std::string const& strKey, std::string const& strValue);

private:
    LocalCredentials (LocalCredentials const&); // disallowed
    LocalCredentials& operator= (const LocalCredentials&); // disallowed

    bool    nodeIdentityLoad ();
    bool    nodeIdentityCreate ();

private:
    std::recursive_mutex mLock;

    RippleAddress   mNodePublicKey;
    RippleAddress   mNodePrivateKey;

    LedgerIndex mLedger; // ledger we last synched to
};

} // ripple

#endif
