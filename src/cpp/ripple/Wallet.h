#ifndef __WALLET__
#define __WALLET__

#include <vector>
#include <map>
#include <string>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/shared_ptr.hpp>

#include "openssl/ec.h"
#include "openssl/dh.h"

class Ledger;

class Wallet
{
private:
    bool    nodeIdentityLoad ();
    bool    nodeIdentityCreate ();

    Wallet (const Wallet&); // no implementation
    Wallet& operator= (const Wallet&); // no implementation

public:
    Wallet ();

    // Begin processing.
    // - Maintain peer connectivity through validation and peer management.
    void start ();

    const RippleAddress&    getNodePublic () const
    {
        return mNodePublicKey;
    }
    const RippleAddress&    getNodePrivate () const
    {
        return mNodePrivateKey;
    }
    DH*         getDh512 ()
    {
        return DHparams_dup (mDh512);
    }
    DH*         getDh1024 ()
    {
        return DHparams_dup (mDh1024);
    }

    // Local persistence of RPC clients
    bool        dataDelete (const std::string& strKey);
    bool        dataFetch (const std::string& strKey, std::string& strValue);
    bool        dataStore (const std::string& strKey, const std::string& strValue);

private:
    boost::recursive_mutex mLock;

    RippleAddress   mNodePublicKey;
    RippleAddress   mNodePrivateKey;
    DH*             mDh512;
    DH*             mDh1024;

    uint32 mLedger; // ledger we last synched to
};

#endif
// vim:ts=4
