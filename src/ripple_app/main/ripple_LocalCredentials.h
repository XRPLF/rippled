//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_LOCALCREDENTIALS_H
#define RIPPLE_LOCALCREDENTIALS_H

/** Holds the cryptographic credentials identifying this instance of the server.
*/
class LocalCredentials : public Uncopyable
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
    boost::recursive_mutex mLock;

    RippleAddress   mNodePublicKey;
    RippleAddress   mNodePrivateKey;

    LedgerIndex mLedger; // ledger we last synched to
};

#endif
// vim:ts=4
