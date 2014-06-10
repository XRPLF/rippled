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

#include <ripple/overlay/impl/PeerImp.h>
#include <ripple/overlay/impl/Tuning.h>
#include <beast/streams/debug_ostream.h>

namespace ripple {

//------------------------------------------------------------------------------

/** A peer has sent us transaction set data */
// VFALCO TODO Make this non-static
static void peerTXData (Job&,
    std::weak_ptr <Peer> wPeer,
    uint256 const& hash,
    std::shared_ptr <protocol::TMLedgerData> pPacket,
    beast::Journal journal)
{
    std::shared_ptr <Peer> peer = wPeer.lock ();
    if (!peer)
        return;

    protocol::TMLedgerData& packet = *pPacket;

    std::list<SHAMapNode> nodeIDs;
    std::list< Blob > nodeData;
    for (int i = 0; i < packet.nodes ().size (); ++i)
    {
        const protocol::TMLedgerNode& node = packet.nodes (i);

        if (!node.has_nodeid () || !node.has_nodedata () || (node.nodeid ().size () != 33))
        {
            journal.warning << "LedgerData request with invalid node ID";
            peer->charge (Resource::feeInvalidRequest);
            return;
        }

        nodeIDs.push_back (SHAMapNode (node.nodeid ().data (), node.nodeid ().size ()));
        nodeData.push_back (Blob (node.nodedata ().begin (), node.nodedata ().end ()));
    }

    SHAMapAddNode san;
    {
        Application::ScopedLockType lock (getApp ().getMasterLock ());

        san =  getApp().getOPs().gotTXData (peer, hash, nodeIDs, nodeData);
    }

    if (san.isInvalid ())
    {
        peer->charge (Resource::feeUnwantedData);
    }
}

// VFALCO NOTE This function is way too big and cumbersome.
void
PeerImp::getLedger (protocol::TMGetLedger& packet)
{
    SHAMap::pointer map;
	protocol::TMLedgerData reply;
	bool fatLeaves = true, fatRoot = false;

	if (packet.has_requestcookie ())
	    reply.set_requestcookie (packet.requestcookie ());

	std::string logMe;

	if (packet.itype () == protocol::liTS_CANDIDATE)
	{
	    // Request is for a transaction candidate set
	    m_journal.trace << "Received request for TX candidate set data "
	                    << to_string (this);

	    if ((!packet.has_ledgerhash () || packet.ledgerhash ().size () != 32))
	    {
	        charge (Resource::feeInvalidRequest);
	        m_journal.warning << "invalid request for TX candidate set data";
	        return;
	    }

	    uint256 txHash;
	    memcpy (txHash.begin (), packet.ledgerhash ().data (), 32);

	    {
	        Application::ScopedLockType lock (getApp ().getMasterLock ());
	        map = getApp().getOPs ().getTXMap (txHash);
	    }

	    if (!map)
	    {
	        if (packet.has_querytype () && !packet.has_requestcookie ())
	        {
	            m_journal.debug << "Trying to route TX set request";

                struct get_usable_peers
                {
                    typedef Overlay::PeerSequence return_type;

                    Overlay::PeerSequence usablePeers;
                    uint256 const& txHash;
                    Peer const* skip;

                    get_usable_peers(uint256 const& hash, Peer const* s)
                        : txHash(hash), skip(s)
                    { }

                    void operator() (Peer::ptr const& peer)
                    {
                        if (peer->hasTxSet (txHash) && (peer.get () != skip))
                            usablePeers.push_back (peer);
                    }

                    return_type operator() ()
                    {
                        return usablePeers;
                    }
                };

	            Overlay::PeerSequence usablePeers (m_overlay.foreach (
                    get_usable_peers (txHash, this)));

                if (usablePeers.empty ())
                {
                    m_journal.info << "Unable to route TX set request";
                    return;
                }

                Peer::ptr const& selectedPeer = usablePeers[rand () % usablePeers.size ()];
                packet.set_requestcookie (getShortId ());
                selectedPeer->send (
                    std::make_shared<Message> (packet, protocol::mtGET_LEDGER));
                return;
	        }

	        m_journal.error << "We do not have the map our peer wants "
	                        << to_string (this);

	        charge (Resource::feeInvalidRequest);
	        return;
	    }

        reply.set_ledgerseq (0);
	    reply.set_ledgerhash (txHash.begin (), txHash.size ());
	    reply.set_type (protocol::liTS_CANDIDATE);
	    fatLeaves = false; // We'll already have most transactions
	    fatRoot = true; // Save a pass
	}
	else
	{
	    if (getApp().getFeeTrack().isLoadedLocal() && !m_clusterNode)
	    {
	        m_journal.debug << "Too busy to fetch ledger data";
	        return;
	    }

	    // Figure out what ledger they want
	    m_journal.trace << "Received request for ledger data "
	                    << to_string (this);
	    Ledger::pointer ledger;

	    if (packet.has_ledgerhash ())
	    {
	        uint256 ledgerhash;

	        if (packet.ledgerhash ().size () != 32)
	        {
	            charge (Resource::feeInvalidRequest);
	            m_journal.warning << "Invalid request";
	            return;
	        }

	        memcpy (ledgerhash.begin (), packet.ledgerhash ().data (), 32);
	        logMe += "LedgerHash:";
	        logMe += to_string (ledgerhash);
	        ledger = getApp().getLedgerMaster ().getLedgerByHash (ledgerhash);

	        if (!ledger && m_journal.trace)
	            m_journal.trace << "Don't have ledger " << ledgerhash;

	        if (!ledger && (packet.has_querytype () && !packet.has_requestcookie ()))
	        {
	            std::uint32_t seq = 0;

	            if (packet.has_ledgerseq ())
	                seq = packet.ledgerseq ();

	            Overlay::PeerSequence peerList = m_overlay.getActivePeers ();
                Overlay::PeerSequence usablePeers;
                BOOST_FOREACH (Peer::ptr const& peer, peerList)
                {
                    if (peer->hasLedger (ledgerhash, seq) && (peer.get () != this))
                        usablePeers.push_back (peer);
                }

                if (usablePeers.empty ())
                {
                    m_journal.trace << "Unable to route ledger request";
                    return;
                }

                Peer::ptr const& selectedPeer = usablePeers[rand () % usablePeers.size ()];
                packet.set_requestcookie (getShortId ());
                selectedPeer->send (
                    std::make_shared<Message> (packet, protocol::mtGET_LEDGER));
                m_journal.debug << "Ledger request routed";
                return;
            }
	    }
	    else if (packet.has_ledgerseq ())
	    {
	        if (packet.ledgerseq() < getApp().getLedgerMaster().getEarliestFetch())
	        {
	            m_journal.debug << "Peer requests early ledger";
	            return;
	        }
	        ledger = getApp().getLedgerMaster ().getLedgerBySeq (packet.ledgerseq ());
	        if (!ledger && m_journal.debug)
	            m_journal.debug << "Don't have ledger " << packet.ledgerseq ();
	    }
	    else if (packet.has_ltype () && (packet.ltype () == protocol::ltCURRENT))
	    {
	        ledger = getApp().getLedgerMaster ().getCurrentLedger ();
	    }
	    else if (packet.has_ltype () && (packet.ltype () == protocol::ltCLOSED) )
	    {
	        ledger = getApp().getLedgerMaster ().getClosedLedger ();

	        if (ledger && !ledger->isClosed ())
	            ledger = getApp().getLedgerMaster ().getLedgerBySeq (ledger->getLedgerSeq () - 1);
	    }
	    else
	    {
	        charge (Resource::feeInvalidRequest);
	        m_journal.warning << "Can't figure out what ledger they want";
	        return;
	    }

	    if ((!ledger) || (packet.has_ledgerseq () && (packet.ledgerseq () != ledger->getLedgerSeq ())))
	    {
	        charge (Resource::feeInvalidRequest);

	        if (m_journal.warning && ledger)
	            m_journal.warning << "Ledger has wrong sequence";

	        return;
	    }

            if (!packet.has_ledgerseq() && (ledger->getLedgerSeq() < getApp().getLedgerMaster().getEarliestFetch()))
            {
                m_journal.debug << "Peer requests early ledger";
                return;
            }

	    // Fill out the reply
	    uint256 lHash = ledger->getHash ();
	    reply.set_ledgerhash (lHash.begin (), lHash.size ());
	    reply.set_ledgerseq (ledger->getLedgerSeq ());
	    reply.set_type (packet.itype ());

	    if (packet.itype () == protocol::liBASE)
	    {
	        // they want the ledger base data
	        m_journal.trace << "They want ledger base data";
	        Serializer nData (128);
	        ledger->addRaw (nData);
	        reply.add_nodes ()->set_nodedata (nData.getDataPtr (), nData.getLength ());

	        SHAMap::pointer map = ledger->peekAccountStateMap ();

	        if (map && map->getHash ().isNonZero ())
	        {
	            // return account state root node if possible
	            Serializer rootNode (768);

	            if (map->getRootNode (rootNode, snfWIRE))
	            {
	                reply.add_nodes ()->set_nodedata (rootNode.getDataPtr (), rootNode.getLength ());

	                if (ledger->getTransHash ().isNonZero ())
	                {
	                    map = ledger->peekTransactionMap ();

	                    if (map && map->getHash ().isNonZero ())
	                    {
	                        rootNode.erase ();

	                        if (map->getRootNode (rootNode, snfWIRE))
	                            reply.add_nodes ()->set_nodedata (rootNode.getDataPtr (), rootNode.getLength ());
	                    }
	                }
	            }
	        }

	        Message::pointer oPacket = std::make_shared<Message> (reply, protocol::mtLEDGER_DATA);
	        send (oPacket);
	        return;
	    }

	    if (packet.itype () == protocol::liTX_NODE)
	    {
	        map = ledger->peekTransactionMap ();
	        logMe += " TX:";
	        logMe += to_string (map->getHash ());
	    }
	    else if (packet.itype () == protocol::liAS_NODE)
	    {
	        map = ledger->peekAccountStateMap ();
	        logMe += " AS:";
	        logMe += to_string (map->getHash ());
	    }
	}

	if (!map || (packet.nodeids_size () == 0))
	{
	    m_journal.warning << "Can't find map or empty request";
	    charge (Resource::feeInvalidRequest);
	    return;
	}

	m_journal.trace << "Request: " << logMe;

	for (int i = 0; i < packet.nodeids ().size (); ++i)
	{
	    SHAMapNode mn (packet.nodeids (i).data (), packet.nodeids (i).size ());

	    if (!mn.isValid ())
	    {
	        m_journal.warning << "Request for invalid node: " << logMe;
	        charge (Resource::feeInvalidRequest);
	        return;
	    }

	    std::vector<SHAMapNode> nodeIDs;
	    std::list< Blob > rawNodes;

	    try
	    {
	        if (map->getNodeFat (mn, nodeIDs, rawNodes, fatRoot, fatLeaves))
	        {
	            assert (nodeIDs.size () == rawNodes.size ());
	            m_journal.trace << "getNodeFat got " << rawNodes.size () << " nodes";
	            std::vector<SHAMapNode>::iterator nodeIDIterator;
	            std::list< Blob >::iterator rawNodeIterator;

	            for (nodeIDIterator = nodeIDs.begin (), rawNodeIterator = rawNodes.begin ();
	                    nodeIDIterator != nodeIDs.end (); ++nodeIDIterator, ++rawNodeIterator)
	            {
	                Serializer nID (33);
	                nodeIDIterator->addIDRaw (nID);
	                protocol::TMLedgerNode* node = reply.add_nodes ();
	                node->set_nodeid (nID.getDataPtr (), nID.getLength ());
	                node->set_nodedata (&rawNodeIterator->front (), rawNodeIterator->size ());
	            }
	        }
	        else
	            m_journal.warning << "getNodeFat returns false";
	    }
	    catch (std::exception&)
	    {
	        std::string info;

	        if (packet.itype () == protocol::liTS_CANDIDATE)
	            info = "TS candidate";
	        else if (packet.itype () == protocol::liBASE)
	            info = "Ledger base";
	        else if (packet.itype () == protocol::liTX_NODE)
	            info = "TX node";
	        else if (packet.itype () == protocol::liAS_NODE)
	            info = "AS node";

	        if (!packet.has_ledgerhash ())
	            info += ", no hash specified";

	        m_journal.warning << "getNodeFat( " << mn << ") throws exception: " << info;
	    }
	}

	Message::pointer oPacket = std::make_shared<Message> (reply, protocol::mtLEDGER_DATA);
	send (oPacket);
}

// This is dispatched by the job queue
static
void
sGetLedger (std::weak_ptr<PeerImp> wPeer,
    std::shared_ptr <protocol::TMGetLedger> packet)
{
    std::shared_ptr<PeerImp> peer = wPeer.lock ();

    if (peer)
        peer->getLedger (*packet);
}

//------------------------------------------------------------------------------

void
PeerImp::async_read_some()
{
    m_socket->async_read_some (read_buffer_.prepare (Tuning::readBufferBytes),
        m_strand.wrap (std::bind (&PeerImp::on_read_some,
            shared_from_this(), beast::asio::placeholders::error,
                beast::asio::placeholders::bytes_transferred)));
}

void
PeerImp::on_read_some (error_code ec, std::size_t bytes_transferred)
{
    if (m_detaching)
        return;

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (! ec)
    {
        read_buffer_.commit (bytes_transferred);
        ec = message_stream_.write_one (read_buffer_.data());
        read_buffer_.consume (bytes_transferred);
    }

    if (ec)
    {
        m_journal.info <<
            "on_read_some: " << ec.message();
        detach("on_read_some");
        return;
    }

    async_read_some();
}

//------------------------------------------------------------------------------
//
// abstract_protocol_handler
//
//------------------------------------------------------------------------------

PeerImp::error_code
PeerImp::on_message_unknown (std::uint16_t type)
{
    error_code ec;
    // TODO
    return ec;
}

PeerImp::error_code
PeerImp::on_message_begin (std::uint16_t type,
    std::shared_ptr <::google::protobuf::Message> const& m)
{
    error_code ec;

#if 0
    beast::debug_ostream log;
    log << m->DebugString();
#endif

    if (type == protocol::mtHELLO && m_state != stateConnected)
    {
        m_journal.warning <<
            "Unexpected TMHello";
        ec = invalid_argument_error();
    }
    else if (type != protocol::mtHELLO && m_state == stateConnected)
    {
        m_journal.warning <<
            "Expected TMHello";
        ec = invalid_argument_error();
    }

    if (! ec)
    {
        load_event_ = getApp().getJobQueue ().getLoadEventAP (
            jtPEER, protocol_message_name(type));
    }

    return ec;
}

void
PeerImp::on_message_end (std::uint16_t,
    std::shared_ptr <::google::protobuf::Message> const&)
{
    load_event_.reset();
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMHello> const& m)
{
    error_code ec;

    bool bDetach (true);

    m_timer.cancel ();

    std::uint32_t const ourTime (getApp().getOPs ().getNetworkTimeNC ());
    std::uint32_t const minTime (ourTime - clockToleranceDeltaSeconds);
    std::uint32_t const maxTime (ourTime + clockToleranceDeltaSeconds);

#ifdef BEAST_DEBUG
    if (m->has_nettime ())
    {
        std::int64_t to = ourTime;
        to -= m->nettime ();
        m_journal.debug <<
            "Connect: time offset " << to;
    }
#endif

    BuildInfo::Protocol protocol (m->protoversion());

    if (m->has_nettime () &&
        ((m->nettime () < minTime) || (m->nettime () > maxTime)))
    {
        if (m->nettime () > maxTime)
        {
            m_journal.info <<
                "Hello: Clock for " << *this <<
                " is off by +" << m->nettime () - ourTime;
        }
        else if (m->nettime () < minTime)
        {
            m_journal.info <<
                "Hello: Clock for " << *this <<
                " is off by -" << ourTime - m->nettime ();
        }
    }
    else if (m->protoversionmin () > BuildInfo::getCurrentProtocol().toPacked ())
    {
        std::string reqVersion (
            protocol.toStdString ());

        std::string curVersion (
            BuildInfo::getCurrentProtocol().toStdString ());

        m_journal.info <<
            "Hello: Disconnect: Protocol mismatch [" <<
            "Peer expects " << reqVersion <<
            " and we run " << curVersion << "]";
    }
    else if (! m_nodePublicKey.setNodePublic (m->nodepublic ()))
    {
        m_journal.info <<
            "Hello: Disconnect: Bad node public key.";
    }
    else if (! m_nodePublicKey.verifyNodePublic (
        m_secureCookie, m->nodeproof (), ECDSA::not_strict))
    {
        // Unable to verify they have private key for claimed public key.
        m_journal.info <<
            "Hello: Disconnect: Failed to verify session.";
    }
    else
    {
        // Successful connection.
        m_journal.info <<
            "Hello: Connect: " << m_nodePublicKey.humanNodePublic ();

        if ((protocol != BuildInfo::getCurrentProtocol()) &&
            m_journal.active(beast::Journal::Severity::kInfo))
        {
            m_journal.info <<
                "Peer protocol: " << protocol.toStdString ();
        }

        mHello = *m;

        // Determine if this peer belongs to our cluster and get it's name
        m_clusterNode = getApp().getUNL().nodeInCluster (
            m_nodePublicKey, m_nodeName);

        if (m_clusterNode)
            m_journal.info <<
            "Connected to cluster node " << m_nodeName;

        assert (m_state == stateConnected);
        m_state = stateHandshaked;

        m_peerFinder.on_handshake (m_slot, RipplePublicKey (m_nodePublicKey),
            m_clusterNode);

        // XXX Set timer: connection is in grace period to be useful.
        // XXX Set timer: connection idle (idle may vary depending on connection type.)
        if ((mHello.has_ledgerclosed ()) && (
            mHello.ledgerclosed ().size () == (256 / 8)))
        {
            memcpy (m_closedLedgerHash.begin (),
                mHello.ledgerclosed ().data (), 256 / 8);

            if ((mHello.has_ledgerprevious ()) &&
                (mHello.ledgerprevious ().size () == (256 / 8)))
            {
                memcpy (m_previousLedgerHash.begin (),
                    mHello.ledgerprevious ().data (), 256 / 8);
                addLedger (m_previousLedgerHash);
            }
            else
            {
                m_previousLedgerHash.zero ();
            }
        }

        bDetach = false;
    }

    if (bDetach)
    {
        //m_nodePublicKey.clear ();
        //detach ("recvh");

        ec = invalid_argument_error();
    }
    else
    {
        sendGetPeers ();
    }

    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMPing> const& m)
{
    error_code ec;
    if (m->type () == protocol::TMPing::ptPING)
    {
        m->set_type (protocol::TMPing::ptPONG);
        send (std::make_shared<Message> (*m, protocol::mtPING));
    }
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMProofWork> const& m)
{
    error_code ec;
    if (m->has_response ())
    {
        // this is an answer to a proof of work we requested
        if (m->response ().size () != (256 / 8))
        {
            charge (Resource::feeInvalidRequest);
            return ec;
        }

        uint256 response;
        memcpy (response.begin (), m->response ().data (), 256 / 8);

        // VFALCO TODO Use a dependency injection here
        PowResult r = getApp().getProofOfWorkFactory ().checkProof (m->token (), response);

        if (r == powOK)
        {
            // credit peer
            // WRITEME
            return ec;
        }

        // return error message
        // WRITEME
        if (r != powTOOEASY)
        {
            charge (Resource::feeBadProofOfWork);
        }

        return ec;
    }

    if (m->has_result ())
    {
        // this is a reply to a proof of work we sent
        // WRITEME
    }

    if (m->has_target () && m->has_challenge () && m->has_iterations ())
    {
        // this is a challenge
        // WRITEME: Reject from inbound connections

        uint256 challenge, target;

        if ((m->challenge ().size () != (256 / 8)) || (m->target ().size () != (256 / 8)))
        {
            charge (Resource::feeInvalidRequest);
            return ec;
        }

        memcpy (challenge.begin (), m->challenge ().data (), 256 / 8);
        memcpy (target.begin (), m->target ().data (), 256 / 8);
        ProofOfWork::pointer pow = std::make_shared<ProofOfWork> (m->token (), m->iterations (),
                                    challenge, target);

        if (!pow->isValid ())
        {
            charge (Resource::feeInvalidRequest);
            return ec;
        }

#if 0   // Until proof of work is completed, don't do it
        getApp().getJobQueue ().addJob (
            jtPROOFWORK,
            "recvProof->doProof",
            std::bind (&PeerImp::doProofOfWork, std::placeholders::_1,
                        std::weak_ptr <Peer> (shared_from_this ()), pow));
#endif

        return ec;
    }

    m_journal.info << "Received in valid proof of work object from peer";

    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMCluster> const& m)
{
    error_code ec;
    if (!m_clusterNode)
    {
        charge (Resource::feeUnwantedData);
        return ec;
    }

    for (int i = 0; i < m->clusternodes().size(); ++i)
    {
        protocol::TMClusterNode const& node = m->clusternodes(i);

        std::string name;
        if (node.has_nodename())
            name = node.nodename();
        ClusterNodeStatus s(name, node.nodeload(), node.reporttime());

        RippleAddress nodePub;
        nodePub.setNodePublic(node.publickey());

        getApp().getUNL().nodeUpdate(nodePub, s);
    }

    int loadSources = m->loadsources().size();
    if (loadSources != 0)
    {
        Resource::Gossip gossip;
        gossip.items.reserve (loadSources);
        for (int i = 0; i < m->loadsources().size(); ++i)
        {
            protocol::TMLoadSource const& node = m->loadsources (i);
            Resource::Gossip::Item item;
            item.address = beast::IP::Endpoint::from_string (node.name());
            item.balance = node.cost();
            if (item.address != beast::IP::Endpoint())
                gossip.items.push_back(item);
        }
        m_resourceManager.importConsumers (m_nodeName, gossip);
    }

    getApp().getFeeTrack().setClusterFee(getApp().getUNL().getClusterFee());
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMGetPeers> const& m)
{
    error_code ec;
    // VFALCO TODO This message is now obsolete due to PeerFinder
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMPeers> const& m)
{
    error_code ec;
    // VFALCO TODO This message is now obsolete due to PeerFinder
    std::vector <beast::IP::Endpoint> list;
    list.reserve (m->nodes().size());
    for (int i = 0; i < m->nodes ().size (); ++i)
    {
        in_addr addr;

        addr.s_addr = m->nodes (i).ipv4 ();

        {
            beast::IP::AddressV4 v4 (ntohl (addr.s_addr));
            beast::IP::Endpoint address (v4, m->nodes (i).ipv4port ());
            list.push_back (address);
        }
    }

    if (! list.empty())
        m_peerFinder.on_legacy_endpoints (list);
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMEndpoints> const& m)
{
    error_code ec;
    std::vector <PeerFinder::Endpoint> endpoints;

    endpoints.reserve (m->endpoints().size());

    for (int i = 0; i < m->endpoints ().size (); ++i)
    {
        PeerFinder::Endpoint endpoint;
        protocol::TMEndpoint const& tm (m->endpoints(i));

        // hops
        endpoint.hops = tm.hops();

        // ipv4
        if (endpoint.hops > 0)
        {
            in_addr addr;
            addr.s_addr = tm.ipv4().ipv4();
            beast::IP::AddressV4 v4 (ntohl (addr.s_addr));
            endpoint.address = beast::IP::Endpoint (v4, tm.ipv4().ipv4port ());
        }
        else
        {
            // This Endpoint describes the peer we are connected to.
            // We will take the remote address seen on the socket and
            // store that in the IP::Endpoint. If this is the first time,
            // then we'll verify that their listener can receive incoming
            // by performing a connectivity test.
            //
            endpoint.address = m_remoteAddress.at_port (
                tm.ipv4().ipv4port ());
        }

        endpoints.push_back (endpoint);
    }

    if (! endpoints.empty())
        m_peerFinder.on_endpoints (m_slot, endpoints);
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMTransaction> const& m)
{
    error_code ec;
    Serializer s (m->rawtransaction ());

    try
    {
        SerializerIterator sit (s);
        SerializedTransaction::pointer stx = std::make_shared<SerializedTransaction> (std::ref (sit));
        uint256 txID = stx->getTransactionID();

        int flags;

        if (! getApp().getHashRouter ().addSuppressionPeer (txID, m_shortId, flags))
        {
            // we have seen this transaction recently
            if (flags & SF_BAD)
            {
                charge (Resource::feeInvalidSignature);
                return ec;
            }

            if (!(flags & SF_RETRY))
                return ec;
        }

        m_journal.debug << "Got transaction from peer " << *this << ": " << txID;

        if (m_clusterNode)
            flags |= SF_TRUSTED | SF_SIGGOOD;

        if (getApp().getJobQueue().getJobCount(jtTRANSACTION) > 100)
            m_journal.info << "Transaction queue is full";
        else if (getApp().getLedgerMaster().getValidatedLedgerAge() > 240)
            m_journal.trace << "No new transactions until synchronized";
        else
            getApp().getJobQueue ().addJob (jtTRANSACTION,
                "recvTransaction->checkTransaction",
                std::bind (
                    &PeerImp::checkTransaction, std::placeholders::_1,
                    flags, stx,
                    std::weak_ptr<Peer> (shared_from_this ())));

    }
    catch (...)
    {
        m_journal.warning << "Transaction invalid: " <<
            s.getHex();
    }
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMGetLedger> const& m)
{
    error_code ec;
    getApp().getJobQueue().addJob (jtPACK, "recvGetLedger",
        std::bind (&sGetLedger, std::weak_ptr<PeerImp> (shared_from_this ()), m));
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMLedgerData> const& m)
{
    error_code ec;
    protocol::TMLedgerData& packet = *m;

    if (m->nodes ().size () <= 0)
    {
        m_journal.warning << "Ledger/TXset data with no nodes";
        return ec;
    }

    if (m->has_requestcookie ())
    {
        Peer::ptr target = m_overlay.findPeerByShortID (m->requestcookie ());

        if (target)
        {
            m->clear_requestcookie ();
            target->send (std::make_shared<Message> (packet, protocol::mtLEDGER_DATA));
        }
        else
        {
            m_journal.info << "Unable to route TX/ledger data reply";
            charge (Resource::feeUnwantedData);
        }

        return ec;
    }

    uint256 hash;

    if (m->ledgerhash ().size () != 32)
    {
        m_journal.warning << "TX candidate reply with invalid hash size";
        charge (Resource::feeInvalidRequest);
        return ec;
    }

    memcpy (hash.begin (), m->ledgerhash ().data (), 32);

    if (m->type () == protocol::liTS_CANDIDATE)
    {
        // got data for a candidate transaction set

        getApp().getJobQueue().addJob (jtTXN_DATA, "recvPeerData",
            std::bind (&peerTXData, std::placeholders::_1,
                std::weak_ptr<Peer> (shared_from_this ()),
                    hash, m, m_journal));

        return ec;
    }

    if (!getApp().getInboundLedgers ().gotLedgerData (hash, shared_from_this(), m))
    {
        m_journal.trace  << "Got data for unwanted ledger";
        charge (Resource::feeUnwantedData);
    }
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMProposeSet> const& m)
{
    error_code ec;
    protocol::TMProposeSet& set = *m;

    // VFALCO Magic numbers are bad
    if ((set.closetime() + 180) < getApp().getOPs().getCloseTimeNC())
        return ec;

    // VFALCO Magic numbers are bad
    // Roll this into a validation function
    if ((set.currenttxhash ().size () != 32) || (set.nodepubkey ().size () < 28) ||
            (set.signature ().size () < 56) || (set.nodepubkey ().size () > 128) || (set.signature ().size () > 128))
    {
        m_journal.warning << "Received proposal is malformed";
        charge (Resource::feeInvalidSignature);
        return ec;
    }

    if (set.has_previousledger () && (set.previousledger ().size () != 32))
    {
        m_journal.warning << "Received proposal is malformed";
        charge (Resource::feeInvalidRequest);
        return ec;
    }

    uint256 proposeHash, prevLedger;
    memcpy (proposeHash.begin (), set.currenttxhash ().data (), 32);

    if (set.has_previousledger ())
        memcpy (prevLedger.begin (), set.previousledger ().data (), 32);

    uint256 suppression = LedgerProposal::computeSuppressionID (proposeHash, prevLedger,
        set.proposeseq(), set.closetime (),
        Blob(set.nodepubkey ().begin (), set.nodepubkey ().end ()),
        Blob(set.signature ().begin (), set.signature ().end ()));

    if (! getApp().getHashRouter ().addSuppressionPeer (suppression, m_shortId))
    {
        m_journal.trace << "Received duplicate proposal from peer " << m_shortId;
        return ec;
    }

    RippleAddress signerPublic = RippleAddress::createNodePublic (strCopy (set.nodepubkey ()));

    if (signerPublic == getConfig ().VALIDATION_PUB)
    {
        m_journal.trace << "Received our own proposal from peer " << m_shortId;
        return ec;
    }

    bool isTrusted = getApp().getUNL ().nodeInUNL (signerPublic);
    if (!isTrusted && getApp().getFeeTrack ().isLoadedLocal ())
    {
        m_journal.debug << "Dropping UNTRUSTED proposal due to load";
        return ec;
    }

    m_journal.trace << "Received " << (isTrusted ? "trusted" : "UNTRUSTED") <<
                        " proposal from " << m_shortId;

    uint256 consensusLCL;

    {
        Application::ScopedLockType lock (getApp ().getMasterLock ());
        consensusLCL = getApp().getOPs ().getConsensusLCL ();
    }

    LedgerProposal::pointer proposal = std::make_shared<LedgerProposal> (
        prevLedger.isNonZero () ? prevLedger : consensusLCL,
        set.proposeseq (), proposeHash, set.closetime (), signerPublic, suppression);

    getApp().getJobQueue ().addJob (isTrusted ? jtPROPOSAL_t : jtPROPOSAL_ut,
        "recvPropose->checkPropose", std::bind (
            &PeerImp::checkPropose, std::placeholders::_1, &m_overlay,
            m, proposal, consensusLCL, m_nodePublicKey,
            std::weak_ptr<Peer> (shared_from_this ()), m_clusterNode));
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMStatusChange> const& m)
{
    error_code ec;
    m_journal.trace << "Received status change from peer " <<
                        to_string (this);

    if (!m->has_networktime ())
        m->set_networktime (getApp().getOPs ().getNetworkTimeNC ());

    if (!mLastStatus.has_newstatus () || m->has_newstatus ())
        mLastStatus = *m;
    else
    {
        // preserve old status
        protocol::NodeStatus status = mLastStatus.newstatus ();
        mLastStatus = *m;
        m->set_newstatus (status);
    }

    if (m->newevent () == protocol::neLOST_SYNC)
    {
        if (!m_closedLedgerHash.isZero ())
        {
            m_journal.trace << "peer has lost sync " << to_string (this);
            m_closedLedgerHash.zero ();
        }

        m_previousLedgerHash.zero ();
        return ec;
    }

    if (m->has_ledgerhash () && (m->ledgerhash ().size () == (256 / 8)))
    {
        // a peer has changed ledgers
        memcpy (m_closedLedgerHash.begin (), m->ledgerhash ().data (), 256 / 8);
        addLedger (m_closedLedgerHash);
        m_journal.trace << "peer LCL is " << m_closedLedgerHash <<
                            " " << to_string (this);
    }
    else
    {
        m_journal.trace << "peer has no ledger hash" << to_string (this);
        m_closedLedgerHash.zero ();
    }

    if (m->has_ledgerhashprevious () && m->ledgerhashprevious ().size () == (256 / 8))
    {
        memcpy (m_previousLedgerHash.begin (), m->ledgerhashprevious ().data (), 256 / 8);
        addLedger (m_previousLedgerHash);
    }
    else m_previousLedgerHash.zero ();

    if (m->has_firstseq () && m->has_lastseq())
    {
        m_minLedger = m->firstseq ();
        m_maxLedger = m->lastseq ();

        // Work around some servers that report sequences incorrectly
        if (m_minLedger == 0)
            m_maxLedger = 0;
        if (m_maxLedger == 0)
            m_minLedger = 0;
    }
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMHaveTransactionSet> const& m)
{
    error_code ec;
    uint256 hashes;

    if (m->hash ().size () != (256 / 8))
    {
        charge (Resource::feeInvalidRequest);
        return ec;
    }

    uint256 hash;

    // VFALCO TODO There should be no use of memcpy() throughout the program.
    //        TODO Clean up this magic number
    //
    memcpy (hash.begin (), m->hash ().data (), 32);

    if (m->status () == protocol::tsHAVE)
        addTxSet (hash);

    {
        Application::ScopedLockType lock (getApp ().getMasterLock ());

        if (!getApp().getOPs ().hasTXSet (shared_from_this (), hash, m->status ()))
            charge (Resource::feeUnwantedData);
    }
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMValidation> const& m)
{
    error_code ec;
    std::uint32_t closeTime = getApp().getOPs().getCloseTimeNC();

    if (m->validation ().size () < 50)
    {
        m_journal.warning << "Too small validation from peer";
        charge (Resource::feeInvalidRequest);
        return ec;
    }

    try
    {
        Serializer s (m->validation ());
        SerializerIterator sit (s);
        SerializedValidation::pointer val = std::make_shared<SerializedValidation> (std::ref (sit), false);

        if (closeTime > (120 + val->getFieldU32(sfSigningTime)))
        {
            m_journal.trace << "Validation is more than two minutes old";
            charge (Resource::feeUnwantedData);
            return ec;
        }

        if (! getApp().getHashRouter ().addSuppressionPeer (s.getSHA512Half(), m_shortId))
        {
            m_journal.trace << "Validation is duplicate";
            return ec;
        }

        bool isTrusted = getApp().getUNL ().nodeInUNL (val->getSignerPublic ());
        if (isTrusted || !getApp().getFeeTrack ().isLoadedLocal ())
        {
            getApp().getJobQueue ().addJob (
                isTrusted ? jtVALIDATION_t : jtVALIDATION_ut,
                "recvValidation->checkValidation",
                std::bind (
                    &PeerImp::checkValidation, std::placeholders::_1,
                    &m_overlay, val, isTrusted, m_clusterNode, m,
                    std::weak_ptr<Peer> (shared_from_this ())));
        }
        else
        {
            m_journal.debug <<
                "Dropping UNTRUSTED validation due to load";
        }
    }
    catch (...)
    {
        m_journal.warning <<
            "Exception processing validation";
        charge (Resource::feeInvalidRequest);
    }

    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMGetObjectByHash> const& m)
{
    error_code ec;
    protocol::TMGetObjectByHash& packet = *m;

    if (packet.query ())
    {
        // this is a query
        if (packet.type () == protocol::TMGetObjectByHash::otFETCH_PACK)
        {
            doFetchPack (m);
            return ec;
        }

        protocol::TMGetObjectByHash reply;

        reply.set_query (false);

        if (packet.has_seq ())
            reply.set_seq (packet.seq ());

        reply.set_type (packet.type ());

        if (packet.has_ledgerhash ())
            reply.set_ledgerhash (packet.ledgerhash ());

        // This is a very minimal implementation
        for (int i = 0; i < packet.objects_size (); ++i)
        {
            uint256 hash;
            const protocol::TMIndexedObject& obj = packet.objects (i);

            if (obj.has_hash () && (obj.hash ().size () == (256 / 8)))
            {
                memcpy (hash.begin (), obj.hash ().data (), 256 / 8);
                // VFALCO TODO Move this someplace more sensible so we dont
                //             need to inject the NodeStore interfaces.
                NodeObject::pointer hObj = getApp().getNodeStore ().fetch (hash);

                if (hObj)
                {
                    protocol::TMIndexedObject& newObj = *reply.add_objects ();
                    newObj.set_hash (hash.begin (), hash.size ());
                    newObj.set_data (&hObj->getData ().front (), hObj->getData ().size ());

                    if (obj.has_nodeid ())
                        newObj.set_index (obj.nodeid ());

                    if (!reply.has_seq () && (hObj->getLedgerIndex () != 0))
                        reply.set_seq (hObj->getLedgerIndex ());
                }
            }
        }

        m_journal.trace << "GetObjByHash had " << reply.objects_size () <<
                            " of " << packet.objects_size () <<
                            " for " << to_string (this);
        send (std::make_shared<Message> (reply, protocol::mtGET_OBJECTS));
    }
    else
    {
        // this is a reply
        std::uint32_t pLSeq = 0;
        bool pLDo = true;
        bool progress = false;

        for (int i = 0; i < packet.objects_size (); ++i)
        {
            const protocol::TMIndexedObject& obj = packet.objects (i);

            if (obj.has_hash () && (obj.hash ().size () == (256 / 8)))
            {
                if (obj.has_ledgerseq ())
                {
                    if (obj.ledgerseq () != pLSeq)
                    {
                        if ((pLDo && (pLSeq != 0)) &&
                            m_journal.active(beast::Journal::Severity::kDebug))
                            m_journal.debug << "Received full fetch pack for " << pLSeq;

                        pLSeq = obj.ledgerseq ();
                        pLDo = !getApp().getOPs ().haveLedger (pLSeq);

                        if (!pLDo)
                                m_journal.debug << "Got pack for " << pLSeq << " too late";
                        else
                            progress = true;
                    }
                }

                if (pLDo)
                {
                    uint256 hash;
                    memcpy (hash.begin (), obj.hash ().data (), 256 / 8);

                    std::shared_ptr< Blob > data (
                        std::make_shared< Blob > (
                            obj.data ().begin (), obj.data ().end ()));

                    getApp().getOPs ().addFetchPack (hash, data);
                }
            }
        }

        if ((pLDo && (pLSeq != 0)) &&
            m_journal.active(beast::Journal::Severity::kDebug))
            m_journal.debug << "Received partial fetch pack for " << pLSeq;

        if (packet.type () == protocol::TMGetObjectByHash::otFETCH_PACK)
            getApp().getOPs ().gotFetchPack (progress, pLSeq);
    }
    return ec;
}

//------------------------------------------------------------------------------

} // ripple
