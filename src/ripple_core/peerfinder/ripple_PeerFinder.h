//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_PEERFINDER_H_INCLUDED
#define RIPPLE_PEERFINDER_H_INCLUDED

/** The identifier we use to track peers in peerfinder
*/
typedef uint160 PeerId;


/** Maintains a set of IP addresses used for getting into the network.
*/
class PeerFinder : public Uncopyable
{
public:
    //--------------------------------------------------------------------------

    /** Describes the state of our currently connected peers
    */
    struct Connections
    {
        int numberIncoming;         // number of inbound Peers
        int numberOutgoing;         // number of outbound Peers

        inline int numberTotal () const noexcept
        {
            return numberIncoming + numberOutgoing;
        }
    };

    //--------------------------------------------------------------------------

    /** An abstract address that can be turned into a socket endpoint.
    */
    struct Address
    {
        virtual String asString () = 0;
    };

    /** An IPv4 address.
    */
    struct AddressIPv4 : Address
    {
        AddressIPv4 (InputParser::IPv4Address const& address, uint16 port)
            : m_address (address)
            , m_port (port)
        {

        }

        String asString ()
        {
            return String () +
                String (m_address.value [0]) + "." +
                String (m_address.value [1]) + "." +
                String (m_address.value [2]) + "." +
                String (m_address.value [3]) + ":" +
                String (m_port);
        }

    private:
        InputParser::IPv4Address m_address;
        uint16 m_port;
    };

    //--------------------------------------------------------------------------

    /** The Callback receives Peerfinder notifications.
        The notifications are sent on a thread owned by the PeerFinder,
        so it is best not to do too much work in here. Just post functor
        to another worker thread or job queue and return.
    */
    struct Callback
    {
        /** Announces our listening ip/port combinations to the network.

            @param address The address to broadcast.
        */
        virtual void onAnnounceAddress () = 0;

        /** Indicates whether or not incoming connections should be accepted.
            When we are full on incoming connections, future incoming
            connections from valid peers should be politely turned away,
            after giving them a random sample of other addresses to try
            from our cache.
        */  
        //virtual void onSetAcceptStatus (bool shouldAcceptIncoming) = 0;

        /** Called periodically to update the callback's list of eligible addresses.
            This is used for making new outgoing connections, for
            handing out addresses to peers, and for periodically seeding the
            network wth hop-limited broadcasts of IP addresses.
        */
        //virtual void onNewAddressesAvailable (std::vector <Address> const& list) = 0;
    };

    //--------------------------------------------------------------------------

    /** Create a new PeerFinder object.
    */
    static PeerFinder* New (Callback& callback);

    /** Destroy the object.

        Any pending source fetch operations are aborted.

        There may be some listener calls made before the
        destructor returns.
    */
    virtual ~PeerFinder () { }

    /** Inform the PeerFinder of the status of our connections.

        This call queues an asynchronous operation to the PeerFinder's thread
        and returns immediately. Normally this is called by the Peer code
        when the counts change.
        
        Thread-safety:
            Safe to call from any thread

        @see Peer
    */
    virtual void updateConnectionsStatus (Connections& connections) = 0;

	
	/** Called when a new peer connection is established. 
		Internally, we add the peer to our tracking table, validate that
		we can connect to it, and begin advertising it to others after
		we are sure that its connection is stable.
	*/
	virtual void onPeerConnected(const PeerId& id) = 0;

	/** Called when an existing peer connection drops for whatever reason.
		Internally, we mark the peer as no longer connected, calculate 
		stability metrics, and consider whether we should try to reconnect
		to it or drop it from our list.
	*/
	virtual void onPeerDisconnected(const PeerId& id) = 0;
};

#endif
