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

#ifndef RIPPLE_PEER_H_INCLUDED
#define RIPPLE_PEER_H_INCLUDED

namespace Resource {
class Charge;
class Manager;
}

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
    static pointer New (Resource::Manager& resourceManager,
                        boost::asio::io_service& io_service,
                        boost::asio::ssl::context& ctx,
                        uint64 id,
                        bool inbound,
                        bool requirePROXYHandshake);

    // VFALCO TODO see if this and below can be private
    virtual void handleConnect (const boost::system::error_code& error,
                                boost::asio::ip::tcp::resolver::iterator it) = 0;

    virtual std::string const& getIP () = 0;

    virtual std::string getDisplayName () = 0;

    virtual int getPort () = 0;

    virtual void setIpPort (const std::string& strIP, int iPort) = 0;

    virtual void connect (const std::string& strIp, int iPort) = 0;

    virtual void connected (const boost::system::error_code& error) = 0;

    virtual void detach (const char*, bool onIOStrand) = 0;

    virtual void sendPacket (const PackedMessage::pointer& packet, bool onStrand) = 0;

    virtual void sendGetPeers () = 0;

    // VFALCO NOTE what's with this odd parameter passing? Why the static member?
    //
    /** Adjust this peer's load balance based on the type of load imposed.

        @note Formerly named punishPeer
    */
    virtual void charge (Resource::Charge const& fee) = 0;
    static  void charge (boost::weak_ptr <Peer>& peer, Resource::Charge const& fee);
    virtual void applyLoadCharge (LoadType) = 0;
    static void applyLoadCharge (boost::weak_ptr <Peer>& peerTOCharge, LoadType loadThatWasImposed);

    virtual Json::Value getJson () = 0;

    virtual bool isConnected () const = 0;

    virtual bool isInCluster () const = 0;

    virtual bool isInbound () const = 0;

    virtual bool isOutbound () const = 0;

    virtual bool getConnectString(std::string&) const = 0;

    virtual uint256 const& getClosedLedgerHash () const = 0;

    virtual bool hasLedger (uint256 const& hash, uint32 seq) const = 0;

    virtual void ledgerRange (uint32& minSeq, uint32& maxSeq) const = 0;

    virtual bool hasTxSet (uint256 const& hash) const = 0;

    virtual uint64 getPeerId () const = 0;

    virtual const RippleAddress& getNodePublic () const = 0;

    virtual void cycleStatus () = 0;

    virtual bool hasProto (int version) = 0;

    virtual bool hasRange (uint32 uMin, uint32 uMax) = 0;

    virtual IPEndpoint getPeerEndpoint() const = 0;

    //--------------------------------------------------------------------------

    typedef boost::asio::ip::tcp::socket NativeSocketType;
    
    virtual NativeSocketType& getNativeSocket () = 0;
};

#endif
