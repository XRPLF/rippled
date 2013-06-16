//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_PEER_H
#define RIPPLE_PEER_H

// VFALCO TODO Couldn't this be a struct?
typedef std::pair <std::string, int> ipPort;

class Peer : public boost::enable_shared_from_this <Peer>
{
public:
    typedef boost::shared_ptr<Peer>         pointer;
    typedef const boost::shared_ptr<Peer>&  ref;

    static int const psbGotHello        = 0;
    static int const psbSentHello       = 1;
    static int const psbInMap           = 2;
    static int const psbTrusted         = 3;
    static int const psbNoLedgers       = 4;
    static int const psbNoTransactions  = 5;
    static int const psbDownLevel       = 6;

public:
    static pointer New (boost::asio::io_service& io_service,
                        boost::asio::ssl::context& ctx,
                        uint64 id,
                        bool inbound);

    // VFALCO TODO see if this and below can be private
    virtual void handleConnect (const boost::system::error_code& error,
                                boost::asio::ip::tcp::resolver::iterator it) = 0;

    virtual std::string& getIP () = 0;

    virtual std::string getDisplayName () = 0;

    virtual int getPort () = 0;

    virtual void setIpPort (const std::string& strIP, int iPort) = 0;

    virtual boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::lowest_layer_type& getSocket () = 0;

    virtual void connect (const std::string& strIp, int iPort) = 0;

    virtual void connected (const boost::system::error_code& error) = 0;

    virtual void detach (const char*, bool onIOStrand) = 0;

    //virtual bool samePeer (Peer::ref p) = 0;
    //virtual bool samePeer (const Peer& p) = 0;

    virtual void sendPacket (const PackedMessage::pointer& packet, bool onStrand) = 0;

    virtual void sendGetPeers () = 0;

    virtual void punishPeer (LoadType) = 0;

    // VFALCO NOTE what's with this odd parameter passing? Why the static member?
    static void punishPeer (const boost::weak_ptr<Peer>&, LoadType);

    virtual Json::Value getJson () = 0;

    virtual bool isConnected () const = 0;

    virtual bool isInbound () const = 0;

    virtual bool isOutbound () const = 0;

    virtual uint256 const& getClosedLedgerHash () const = 0;

    virtual bool hasLedger (uint256 const& hash, uint32 seq) const = 0;

    virtual bool hasTxSet (uint256 const& hash) const = 0;

    virtual uint64 getPeerId () const = 0;

    virtual const RippleAddress& getNodePublic () const = 0;

    virtual void cycleStatus () = 0;

    virtual bool hasProto (int version) = 0;

    virtual bool hasRange (uint32 uMin, uint32 uMax) = 0;
};

#endif
// vim:ts=4
