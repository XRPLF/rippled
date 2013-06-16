//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_PATHREQUEST_H
#define RIPPLE_PATHREQUEST_H

// A pathfinding request submitted by a client
// The request issuer must maintain a strong pointer

class InfoSub;
class STAmount;
class RippleLineCache;

// Return values from parseJson <0 = invalid, >0 = valid
#define PFR_PJ_INVALID              -1
#define PFR_PJ_NOCHANGE             0
#define PFR_PJ_CHANGE               1

class PathRequest : public boost::enable_shared_from_this<PathRequest>
{
public:
    typedef boost::weak_ptr<PathRequest>        wptr;
    typedef boost::shared_ptr<PathRequest>  pointer;
    typedef const pointer&                  ref;
    typedef const wptr&                     wref;
    typedef std::pair<uint160, uint160>     currIssuer_t;

public:
    // VFALCO TODO Break the cyclic dependency on InfoSub
    explicit PathRequest (boost::shared_ptr <InfoSub> const& subscriber);

    bool        isValid (const boost::shared_ptr<Ledger>&);
    bool        isValid ();
    bool        isNew ();
    Json::Value getStatus ();

    Json::Value doCreate (const boost::shared_ptr<Ledger>&, const Json::Value&);
    Json::Value doClose (const Json::Value&);
    Json::Value doStatus (const Json::Value&);

    bool        doUpdate (const boost::shared_ptr<RippleLineCache>&, bool fast); // update jvStatus

    static void updateAll (const boost::shared_ptr<Ledger>& ledger, bool newOnly);

private:
    boost::recursive_mutex          mLock;
    boost::weak_ptr<InfoSub>        wpSubscriber;               // Who this request came from
    Json::Value                     jvId;
    Json::Value                     jvStatus;                   // Last result

    // Client request parameters
    RippleAddress                   raSrcAccount;
    RippleAddress                   raDstAccount;
    STAmount                        saDstAmount;
    std::set<currIssuer_t>          sciSourceCurrencies;
    std::vector<Json::Value>        vjvBridges;

    bool                            bValid;
    bool                            bNew;

    // Track all requests
    static std::set<wptr>           sRequests;
    static boost::recursive_mutex   sLock;

    void setValid ();
    int parseJson (const Json::Value&, bool complete);
};

#endif

// vim:ts=4
