
LedgerAcquire::pointer LedgerAcquireMaster::findCreate(uint256 const& hash, uint32 seq)
{
	assert(hash.isNonZero());
	boost::mutex::scoped_lock sl(mLock);
	LedgerAcquire::pointer& ptr = mLedgers[hash];
	if (ptr)
	{
		ptr->touch();
		return ptr;
	}
	ptr = boost::make_shared<LedgerAcquire>(hash, seq);
	if (!ptr->isDone())
	{
		ptr->addPeers();
		ptr->setTimer(); // Cannot call in constructor
	}
	else
	{
		Ledger::pointer ledger = ptr->getLedger();
		ledger->setClosed();
		ledger->setImmutable();
		theApp->getLedgerMaster().storeLedger(ledger);
		WriteLog (lsDEBUG, LedgerAcquire) << "Acquiring ledger we already have: " << hash;
	}
	return ptr;
}

LedgerAcquire::pointer LedgerAcquireMaster::find(uint256 const& hash)
{
	assert(hash.isNonZero());
	boost::mutex::scoped_lock sl(mLock);
	std::map<uint256, LedgerAcquire::pointer>::iterator it = mLedgers.find(hash);
	if (it != mLedgers.end())
	{
		it->second->touch();
		return it->second;
	}
	return LedgerAcquire::pointer();
}

bool LedgerAcquireMaster::hasLedger(uint256 const& hash)
{
	assert(hash.isNonZero());
	boost::mutex::scoped_lock sl(mLock);
	return mLedgers.find(hash) != mLedgers.end();
}

void LedgerAcquireMaster::dropLedger(uint256 const& hash)
{
	assert(hash.isNonZero());
	boost::mutex::scoped_lock sl(mLock);
	mLedgers.erase(hash);
}

bool LedgerAcquireMaster::awaitLedgerData(uint256 const& ledgerHash)
{
	LedgerAcquire::pointer ledger = find(ledgerHash);
	if (!ledger)
		return false;
	ledger->awaitData();
	return true;
}

void LedgerAcquireMaster::gotLedgerData(Job&, uint256 hash,
	boost::shared_ptr<ripple::TMLedgerData> packet_ptr,	boost::weak_ptr<Peer> wPeer)
{
	ripple::TMLedgerData& packet = *packet_ptr;
	Peer::pointer peer = wPeer.lock();

	WriteLog (lsTRACE, LedgerAcquire) << "Got data (" << packet.nodes().size() << ") for acquiring ledger: " << hash;

	LedgerAcquire::pointer ledger = find(hash);
	if (!ledger)
	{
		WriteLog (lsTRACE, LedgerAcquire) << "Got data for ledger we're not acquiring";
		if (peer)
			peer->punishPeer(LT_InvalidRequest);
		return;
	}
	ledger->noAwaitData();

	if (!peer)
		return;

	if (packet.type() == ripple::liBASE)
	{
		if (packet.nodes_size() < 1)
		{
			WriteLog (lsWARNING, LedgerAcquire) << "Got empty base data";
			peer->punishPeer(LT_InvalidRequest);
			return;
		}
		if (!ledger->takeBase(packet.nodes(0).nodedata()))
		{
			WriteLog (lsWARNING, LedgerAcquire) << "Got invalid base data";
			peer->punishPeer(LT_InvalidRequest);
			return;
		}
		SHAMapAddNode san = SHAMapAddNode::useful();
		if ((packet.nodes().size() > 1) && !ledger->takeAsRootNode(strCopy(packet.nodes(1).nodedata()), san))
		{
			WriteLog (lsWARNING, LedgerAcquire) << "Included ASbase invalid";
		}
		if ((packet.nodes().size() > 2) && !ledger->takeTxRootNode(strCopy(packet.nodes(2).nodedata()), san))
		{
			WriteLog (lsWARNING, LedgerAcquire) << "Included TXbase invalid";
		}
		if (!san.isInvalid())
		{
			ledger->progress();
			ledger->trigger(peer);
		}
		else
			WriteLog (lsDEBUG, LedgerAcquire) << "Peer sends invalid base data";
		return;
	}

	if ((packet.type() == ripple::liTX_NODE) || (packet.type() == ripple::liAS_NODE))
	{
		std::list<SHAMapNode> nodeIDs;
		std::list< Blob > nodeData;

		if (packet.nodes().size() <= 0)
		{
			WriteLog (lsINFO, LedgerAcquire) << "Got response with no nodes";
			peer->punishPeer(LT_InvalidRequest);
			return;
		}
		for (int i = 0; i < packet.nodes().size(); ++i)
		{
			const ripple::TMLedgerNode& node = packet.nodes(i);
			if (!node.has_nodeid() || !node.has_nodedata())
			{
				WriteLog (lsWARNING, LedgerAcquire) << "Got bad node";
				peer->punishPeer(LT_InvalidRequest);
				return;
			}

			nodeIDs.push_back(SHAMapNode(node.nodeid().data(), node.nodeid().size()));
			nodeData.push_back(Blob (node.nodedata().begin(), node.nodedata().end()));
		}
		SHAMapAddNode ret;
		if (packet.type() == ripple::liTX_NODE)
			ledger->takeTxNode(nodeIDs, nodeData, ret);
		else
			ledger->takeAsNode(nodeIDs, nodeData, ret);
		if (!ret.isInvalid())
		{
			ledger->progress();
			ledger->trigger(peer);
		}
		else
			WriteLog (lsDEBUG, LedgerAcquire) << "Peer sends invalid node data";
		return;
	}

	WriteLog (lsWARNING, LedgerAcquire) << "Not sure what ledger data we got";
	peer->punishPeer(LT_InvalidRequest);
}

void LedgerAcquireMaster::sweep()
{
	mRecentFailures.sweep();

	int now = UptimeTimer::getInstance().getElapsedSeconds();
	boost::mutex::scoped_lock sl(mLock);

	std::map<uint256, LedgerAcquire::pointer>::iterator it = mLedgers.begin();
	while (it != mLedgers.end())
	{
		if (it->second->getLastAction() > now)
		{
			it->second->touch();
			++it;
		}
		else if ((it->second->getLastAction() + 60) < now)
			mLedgers.erase(it++);
		else
			++it;
	}
}

int LedgerAcquireMaster::getFetchCount(int& timeoutCount)
{
	timeoutCount = 0;
	int ret = 0;
	{
		typedef std::pair<uint256, LedgerAcquire::pointer> u256_acq_pair;
		boost::mutex::scoped_lock sl(mLock);
		BOOST_FOREACH(const u256_acq_pair& it, mLedgers)
		{
			if (it.second->isActive())
			{
				++ret;
				timeoutCount += it.second->getTimeouts();
			}
		}
	}
	return ret;
}

void LedgerAcquireMaster::gotFetchPack(Job&)
{
	std::vector<LedgerAcquire::pointer> acquires;
	{
		boost::mutex::scoped_lock sl(mLock);

		acquires.reserve(mLedgers.size());
		typedef std::pair<uint256, LedgerAcquire::pointer> u256_acq_pair;
		BOOST_FOREACH(const u256_acq_pair& it, mLedgers)
			acquires.push_back(it.second);
	}

	BOOST_FOREACH(const LedgerAcquire::pointer& acquire, acquires)
	{
		acquire->checkLocal();
	}
}

// vim:ts=4
