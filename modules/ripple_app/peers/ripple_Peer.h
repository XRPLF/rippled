//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_PEER_H_INCLUDED
#define RIPPLE_PEER_H_INCLUDED

// VFALCO TODO Couldn't this be a struct?
typedef std::pair <std::string, int> IPAndPortNumber;

/** Represents a peer connection in the overlay.
*/
class Peer
    : public boost::enable_shared_from_this <Peer>
    , LeakChecked <Peer>
{
public:
    typedef boost::shared_ptr <Peer> pointer;
    typedef pointer const& ref;

public:
    typedef boost::asio::ip::tcp::socket SocketType;
    typedef boost::asio::ssl::stream <SocketType&> StreamType;

    static pointer New (boost::asio::io_service& io_service,
                        boost::asio::ssl::context& ctx,
                        uint64 id,
                        bool inbound);

    // VFALCO TODO see if this and below can be private
    virtual void handleConnect (const boost::system::error_code& error,
                                boost::asio::ip::tcp::resolver::iterator it) = 0;

    virtual std::string const& getIP () = 0;

    virtual std::string getDisplayName () = 0;

    virtual int getPort () = 0;

    virtual void setIpPort (const std::string& strIP, int iPort) = 0;

    virtual SocketType& getSocket () = 0;

    virtual void connect (const std::string& strIp, int iPort) = 0;

    virtual void connected (const boost::system::error_code& error) = 0;

    virtual void detach (const char*, bool onIOStrand) = 0;

    virtual void sendPacket (const PackedMessage::pointer& packet, bool onStrand) = 0;

    virtual void sendGetPeers () = 0;

    virtual void applyLoadCharge (LoadType) = 0;

    // VFALCO NOTE what's with this odd parameter passing? Why the static member?
    //
    /** Adjust this peer's load balance based on the type of load imposed.

        @note Formerly named punishPeer
    */
    static void applyLoadCharge (boost::weak_ptr <Peer>& peerTOCharge, LoadType loadThatWasImposed);

    virtual Json::Value getJson () = 0;

    virtual bool isConnected () const = 0;

    virtual bool isInCluster () const = 0;

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
