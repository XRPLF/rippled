
SETUP_LOG (TransactionAcquire)

#define TX_ACQUIRE_TIMEOUT	250

typedef std::map<uint160, LedgerProposal::pointer>::value_type u160_prop_pair;
typedef std::map<uint256, LCTransaction::pointer>::value_type u256_lct_pair;

DECLARE_INSTANCE(TransactionAcquire);

TransactionAcquire::TransactionAcquire(const uint256& hash) : PeerSet(hash, TX_ACQUIRE_TIMEOUT), mHaveRoot(false)
{
	mMap = boost::make_shared<SHAMap>(smtTRANSACTION, hash);
}

static void TACompletionHandler(uint256 hash, SHAMap::pointer map)
{
	boost::recursive_mutex::scoped_lock sl(theApp->getMasterLock());
	theApp->getOPs().mapComplete(hash, map);
	theApp->getMasterLedgerAcquire().dropLedger(hash);
}

void TransactionAcquire::done()
{ // We hold a PeerSet lock and so cannot acquire the master lock here
	SHAMap::pointer map;
	if (mFailed)
	{
		WriteLog (lsWARNING, TransactionAcquire) << "Failed to acquire TX set " << mHash;
	}
	else
	{
		WriteLog (lsINFO, TransactionAcquire) << "Acquired TX set " << mHash;
		mMap->setImmutable();
		map = mMap;
	}
	theApp->getIOService().post(boost::bind(&TACompletionHandler, mHash, map));
}

void TransactionAcquire::onTimer(bool progress)
{
	bool aggressive = false;
	if (getTimeouts() > 10)
	{
		WriteLog (lsWARNING, TransactionAcquire) << "Ten timeouts on TX set " << getHash();
		{
			boost::recursive_mutex::scoped_lock sl(theApp->getMasterLock());
			if (theApp->getOPs().stillNeedTXSet(mHash))
			{
				WriteLog (lsWARNING, TransactionAcquire) << "Still need it";
				mTimeouts = 0;
				aggressive = true;
			}
		}
		if (!aggressive)
		{
			mFailed = true;
			done();
			return;
		}
	}
	if (aggressive || !getPeerCount())
	{ // out of peers
		WriteLog (lsWARNING, TransactionAcquire) << "Out of peers for TX set " << getHash();

		bool found = false;
		std::vector<Peer::pointer> peerList = theApp->getConnectionPool().getPeerVector();
		BOOST_FOREACH(Peer::ref peer, peerList)
		{
			if (peer->hasTxSet(getHash()))
			{
				found = true;
				peerHas(peer);
			}
		}
		if (!found)
		{
			BOOST_FOREACH(Peer::ref peer, peerList)
				peerHas(peer);
		}
	}
	else if (!progress)
		trigger(Peer::pointer());
}

boost::weak_ptr<PeerSet> TransactionAcquire::pmDowncast()
{
	return boost::dynamic_pointer_cast<PeerSet>(shared_from_this());
}

void TransactionAcquire::trigger(Peer::ref peer)
{
	if (mComplete || mFailed)
	{
		WriteLog (lsINFO, TransactionAcquire) << "complete or failed";
		return;
	}
	if (!mHaveRoot)
	{
		WriteLog (lsTRACE, TransactionAcquire) << "TransactionAcquire::trigger " << (peer ? "havePeer" : "noPeer") << " no root";
		ripple::TMGetLedger tmGL;
		tmGL.set_ledgerhash(mHash.begin(), mHash.size());
		tmGL.set_itype(ripple::liTS_CANDIDATE);
		if (getTimeouts() != 0)
			tmGL.set_querytype(ripple::qtINDIRECT);
		*(tmGL.add_nodeids()) = SHAMapNode().getRawString();
		sendRequest(tmGL, peer);
	}
	else
	{
		std::vector<SHAMapNode> nodeIDs;
		std::vector<uint256> nodeHashes;
		ConsensusTransSetSF sf;
		mMap->getMissingNodes(nodeIDs, nodeHashes, 256, &sf);
		if (nodeIDs.empty())
		{
			if (mMap->isValid())
				mComplete = true;
			else
				mFailed = true;
			done();
			return;
		}
		ripple::TMGetLedger tmGL;
		tmGL.set_ledgerhash(mHash.begin(), mHash.size());
		tmGL.set_itype(ripple::liTS_CANDIDATE);
		if (getTimeouts() != 0)
			tmGL.set_querytype(ripple::qtINDIRECT);
		BOOST_FOREACH(SHAMapNode& it, nodeIDs)
			*(tmGL.add_nodeids()) = it.getRawString();
		sendRequest(tmGL, peer);
	}
}

SMAddNode TransactionAcquire::takeNodes(const std::list<SHAMapNode>& nodeIDs,
	const std::list< std::vector<unsigned char> >& data, Peer::ref peer)
{
	if (mComplete)
	{
		WriteLog (lsTRACE, TransactionAcquire) << "TX set complete";
		return SMAddNode();
	}
	if (mFailed)
	{
		WriteLog (lsTRACE, TransactionAcquire) << "TX set failed";
		return SMAddNode();
	}
	try
	{
		if (nodeIDs.empty())
			return SMAddNode::invalid();
		std::list<SHAMapNode>::const_iterator nodeIDit = nodeIDs.begin();
		std::list< std::vector<unsigned char> >::const_iterator nodeDatait = data.begin();
		ConsensusTransSetSF sf;
		while (nodeIDit != nodeIDs.end())
		{
			if (nodeIDit->isRoot())
			{
				if (mHaveRoot)
				{
					WriteLog (lsWARNING, TransactionAcquire) << "Got root TXS node, already have it";
					return SMAddNode();
				}
				if (!mMap->addRootNode(getHash(), *nodeDatait, snfWIRE, NULL))
				{
					WriteLog (lsWARNING, TransactionAcquire) << "TX acquire got bad root node";
					return SMAddNode::invalid();
				}
				else
					mHaveRoot = true;
			}
			else if (!mMap->addKnownNode(*nodeIDit, *nodeDatait, &sf))
			{
				WriteLog (lsWARNING, TransactionAcquire) << "TX acquire got bad non-root node";
				return SMAddNode::invalid();
			}
			++nodeIDit;
			++nodeDatait;
		}
		trigger(peer);
		progress();
		return SMAddNode::useful();
	}
	catch (...)
	{
		WriteLog (lsERROR, TransactionAcquire) << "Peer sends us junky transaction node data";
		return SMAddNode::invalid();
	}
}

void ConsensusTransSetSF::gotNode(bool fromFilter, const SHAMapNode& id, const uint256& nodeHash,
	const std::vector<unsigned char>& nodeData, SHAMapTreeNode::TNType type)
{
	if (fromFilter)
		return;
	theApp->getTempNodeCache().store(nodeHash, nodeData);
	if ((type == SHAMapTreeNode::tnTRANSACTION_NM) && (nodeData.size() > 16))
	{ // this is a transaction, and we didn't have it
		WriteLog (lsDEBUG, TransactionAcquire) << "Node on our acquiring TX set is TXN we don't have";
		try
		{
			Serializer s(nodeData.begin() + 4, nodeData.end()); // skip prefix
			SerializerIterator sit(s);
			SerializedTransaction::pointer stx = boost::make_shared<SerializedTransaction>(boost::ref(sit));
			assert(stx->getTransactionID() == nodeHash);
			theApp->getJobQueue().addJob(jtTRANSACTION, "TXS->TXN",
				BIND_TYPE(&NetworkOPs::submitTransaction, &theApp->getOPs(), P_1, stx, NetworkOPs::stCallback()));
		}
		catch (...)
		{
			WriteLog (lsWARNING, TransactionAcquire) << "Fetched invalid transaction in proposed set";
		}
	}
}

bool ConsensusTransSetSF::haveNode(const SHAMapNode& id, const uint256& nodeHash,
	std::vector<unsigned char>& nodeData)
{
	if (theApp->getTempNodeCache().retrieve(nodeHash, nodeData))
		return true;

	Transaction::pointer txn = Transaction::load(nodeHash);
	if (txn)
	{ // this is a transaction, and we have it
		WriteLog (lsDEBUG, TransactionAcquire) << "Node in our acquiring TX set is TXN we have";
		Serializer s;
		s.add32(sHP_TransactionID);
		txn->getSTransaction()->add(s, true);
		assert(s.getSHA512Half() == nodeHash);
		nodeData = s.peekData();
		return true;
	}

	return false;
}
