#ifndef RIPPLE_LOCALCREDENTIALS_H
#define RIPPLE_LOCALCREDENTIALS_H

/** Holds the cryptographic credentials identifying this instance of the server.
*/
class LocalCredentials // derive from Uncopyable
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

    DH* getDh512 () const
    {
        return DHparams_dup (mDh512);
    }

    DH* getDh1024 () const
    {
        return DHparams_dup (mDh1024);
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
    DH*             mDh512;
    DH*             mDh1024;

    LedgerIndex mLedger; // ledger we last synched to
};

#endif
// vim:ts=4
