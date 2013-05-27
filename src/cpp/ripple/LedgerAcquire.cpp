
SETUP_LOG (LedgerAcquire)

DECLARE_INSTANCE(LedgerAcquire);

#define LA_DEBUG
#define LEDGER_ACQUIRE_TIMEOUT		2000	// millisecond for each ledger timeout
#define LEDGER_TIMEOUT_COUNT		10 		// how many timeouts before we giveup
#define LEDGER_TIMEOUT_AGGRESSIVE	6		// how many timeouts before we get aggressive
#define TRUST_NETWORK

PeerSet::PeerSet(const uint256& hash, int interval) : mHash(hash), mTimerInterval(interval), mTimeouts(0),
	mComplete(false), mFailed(false), mProgress(true), mAggressive(false), mTimer(theApp->getIOService())
{
	mLastAction = UptimeTimer::getInstance().getElapsedSeconds();
	assert((mTimerInterval > 10) && (mTimerInterval < 30000));
}

void PeerSet::peerHas(Peer::ref ptr)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (!mPeers.insert(std::make_pair(ptr->getPeerId(), 0)).second)
		return;
	newPeer(ptr);
}

void PeerSet::badPeer(Peer::ref ptr)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	mPeers.erase(ptr->getPeerId());
}

void PeerSet::setTimer()
{
	mTimer.expires_from_now(boost::posix_time::milliseconds(mTimerInterval));
	mTimer.async_wait(boost::bind(&PeerSet::TimerEntry, pmDowncast(), boost::asio::placeholders::error));
}

void PeerSet::invokeOnTimer()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (isDone())
		return;

	if (!mProgress)
	{
		++mTimeouts;
		WriteLog (lsWARNING, LedgerAcquire) << "Timeout(" << mTimeouts << ") pc=" << mPeers.size() << " acquiring " << mHash;
		onTimer(false);
	}
	else
	{
		mProgress = false;
		onTimer(true);
	}

	if (!isDone())
		setTimer();
}

void PeerSet::TimerEntry(boost::weak_ptr<PeerSet> wptr, const boost::system::error_code& result)
{
	if (result == boost::asio::error::operation_aborted)
		return;
	boost::shared_ptr<PeerSet> ptr = wptr.lock();
	if (ptr)
	{
		int jc = theApp->getJobQueue().getJobCountTotal(jtLEDGER_DATA);
		if (jc > 4)
		{
			WriteLog (lsDEBUG, LedgerAcquire) << "Deferring PeerSet timer due to load";
			ptr->setTimer();
		}
		else
			theApp->getJobQueue().addJob(jtLEDGER_DATA, "timerEntry", BIND_TYPE(&PeerSet::TimerJobEntry, P_1, ptr));
	}
}

void PeerSet::TimerJobEntry(Job&, boost::shared_ptr<PeerSet> ptr)
{
	ptr->invokeOnTimer();
}

bool PeerSet::isActive()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return !isDone();
}

LedgerAcquire::LedgerAcquire(const uint256& hash, uint32 seq) : PeerSet(hash, LEDGER_ACQUIRE_TIMEOUT),
	mHaveBase(false), mHaveState(false), mHaveTransactions(false), mAborted(false), mSignaled(false), mAccept(false),
	mByHash(true), mWaitCount(0), mSeq(seq)
{
#ifdef LA_DEBUG
	WriteLog (lsTRACE, LedgerAcquire) << "Acquiring ledger " << mHash;
#endif
	tryLocal();
}

void LedgerAcquire::checkLocal()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (isDone())
		return;

	if (tryLocal())
		done();
}

bool LedgerAcquire::tryLocal()
{ // return value: true = no more work to do

	if (!mHaveBase)
	{
		// Nothing we can do without the ledger base
		HashedObject::pointer node = theApp->getHashedObjectStore().retrieve(mHash);
		if (!node)
		{
			std::vector<unsigned char> data;
			if (!theApp->getOPs().getFetchPack(mHash, data))
				return false;
			WriteLog (lsTRACE, LedgerAcquire) << "Ledger base found in fetch pack";
			mLedger = boost::make_shared<Ledger>(data, true);
			theApp->getHashedObjectStore().store(hotLEDGER, mLedger->getLedgerSeq(), data, mHash);
		}
		else
		{
			mLedger = boost::make_shared<Ledger>(strCopy(node->getData()), true);
		}

		if (mLedger->getHash() != mHash)
		{ // We know for a fact the ledger can never be acquired
			WriteLog (lsWARNING, LedgerAcquire) << mHash << " cannot be a ledger";
			mFailed = true;
			return true;
		}
		mHaveBase = true;
	}

	if (!mHaveTransactions)
	{
		if (mLedger->getTransHash().isZero())
		{
			WriteLog (lsTRACE, LedgerAcquire) << "No TXNs to fetch";
			mHaveTransactions = true;
		}
		else
		{
			try
			{
				TransactionStateSF filter(mLedger->getLedgerSeq());
				mLedger->peekTransactionMap()->fetchRoot(mLedger->getTransHash(), &filter);
				WriteLog (lsTRACE, LedgerAcquire) << "Got root txn map locally";
				std::vector<uint256> h = mLedger->getNeededTransactionHashes(1, &filter);
				if (h.empty())
				{
					WriteLog (lsTRACE, LedgerAcquire) << "Had full txn map locally";
					mHaveTransactions = true;
				}
			}
			catch (SHAMapMissingNode&)
			{
			}
		}
	}

	if (!mHaveState)
	{
		if (mLedger->getAccountHash().isZero())
		{
			WriteLog (lsFATAL, LedgerAcquire) << "We are acquiring a ledger with a zero account hash";
			mHaveState = true;
		}
		else
		{
			try
			{
				AccountStateSF filter(mLedger->getLedgerSeq());
				mLedger->peekAccountStateMap()->fetchRoot(mLedger->getAccountHash(), &filter);
				WriteLog (lsTRACE, LedgerAcquire) << "Got root AS map locally";
				std::vector<uint256> h = mLedger->getNeededAccountStateHashes(1, &filter);
				if (h.empty())
				{
					WriteLog (lsTRACE, LedgerAcquire) << "Had full AS map locally";
					mHaveState = true;
				}
			}
			catch (SHAMapMissingNode&)
			{
			}
		}
	}

	if (mHaveTransactions && mHaveState)
	{
		WriteLog (lsDEBUG, LedgerAcquire) << "Had everything locally";
		mComplete = true;
		mLedger->setClosed();
		mLedger->setImmutable();
	}

	return mComplete;
}

void LedgerAcquire::onTimer(bool progress)
{
	mRecentTXNodes.clear();
	mRecentASNodes.clear();

	if (getTimeouts() > LEDGER_TIMEOUT_COUNT)
	{
		WriteLog (lsWARNING, LedgerAcquire) << "Too many timeouts( " << getTimeouts() << ") for ledger " << mHash;
		setFailed();
		done();
		return;
	}

	if (!progress)
	{
		mAggressive = true;
		mByHash = true;
		int pc = getPeerCount();
		WriteLog (lsDEBUG, LedgerAcquire) << "No progress(" << pc << ") for ledger " << pc <<  mHash;
		if (pc == 0)
			addPeers();
		else
			trigger(Peer::pointer());
	}
}

void LedgerAcquire::awaitData()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	++mWaitCount;
}

void LedgerAcquire::noAwaitData()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (mWaitCount > 0 ) --mWaitCount;
}

void LedgerAcquire::addPeers()
{
	std::vector<Peer::pointer> peerList = theApp->getConnectionPool().getPeerVector();

	int vSize = peerList.size();
	if (vSize == 0)
		return;

	// We traverse the peer list in random order so as not to favor any particular peer
	int firstPeer = rand() & vSize;

	int found = 0;
	for (int i = 0; i < vSize; ++i)
	{
		Peer::ref peer = peerList[(i + firstPeer) % vSize];
		if (peer->hasLedger(getHash(), mSeq))
		{
			peerHas(peer);
			if (++found == 3)
				break;
		}
	}

	if (!found)
		for (int i = 0; i < vSize; ++i)
			peerHas(peerList[(i + firstPeer) % vSize]);
}

boost::weak_ptr<PeerSet> LedgerAcquire::pmDowncast()
{
	return boost::dynamic_pointer_cast<PeerSet>(shared_from_this());
}

static void LADispatch(
	Job& job,
	LedgerAcquire::pointer la,
	std::vector< FUNCTION_TYPE<void (LedgerAcquire::pointer)> > trig)
{
	for (unsigned int i = 0; i < trig.size(); ++i)
		trig[i](la);
}

void LedgerAcquire::done()
{
	if (mSignaled)
		return;
	mSignaled = true;
	touch();

#ifdef LA_DEBUG
	WriteLog (lsTRACE, LedgerAcquire) << "Done acquiring ledger " << mHash;
#endif

	assert(isComplete() || isFailed());

	std::vector< FUNCTION_TYPE<void (LedgerAcquire::pointer)> > triggers;
	{
		boost::recursive_mutex::scoped_lock sl(mLock);
		triggers.swap(mOnComplete);
	}

	if (isComplete() && !isFailed() && mLedger)
	{
		mLedger->setClosed();
		mLedger->setImmutable();
		if (mAccept)
			mLedger->setAccepted();
		theApp->getLedgerMaster().storeLedger(mLedger);
	}
	else
		theApp->getMasterLedgerAcquire().logFailure(mHash);

	if (!triggers.empty()) // We hold the PeerSet lock, so must dispatch
		theApp->getJobQueue().addJob(jtLEDGER_DATA, "triggers",
			BIND_TYPE(LADispatch, P_1, shared_from_this(), triggers));
}

bool LedgerAcquire::addOnComplete(FUNCTION_TYPE<void (LedgerAcquire::pointer)> trigger)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (isDone())
		return false;
	mOnComplete.push_back(trigger);
	return true;
}

void LedgerAcquire::trigger(Peer::ref peer)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (isDone())
	{
		WriteLog (lsDEBUG, LedgerAcquire) << "Trigger on ledger: " << mHash <<
			(mAborted ? " aborted": "") << (mComplete ? " completed": "") << (mFailed ? " failed" : "");
		return;
	}

	if ((mWaitCount > 0) && peer)
	{
		mRecentPeers.push_back(peer->getPeerId());
		WriteLog (lsTRACE, LedgerAcquire) << "Deferring peer";
		return;
	}

	if (ShouldLog (lsTRACE, LedgerAcquire))
	{
		if (peer)
			WriteLog (lsTRACE, LedgerAcquire) << "Trigger acquiring ledger " << mHash << " from " << peer->getIP();
		else
			WriteLog (lsTRACE, LedgerAcquire) << "Trigger acquiring ledger " << mHash;
		if (mComplete || mFailed)
			WriteLog (lsTRACE, LedgerAcquire) << "complete=" << mComplete << " failed=" << mFailed;
		else
			WriteLog (lsTRACE, LedgerAcquire) << "base=" << mHaveBase << " tx=" << mHaveTransactions << " as=" << mHaveState;
	}

	if (!mHaveBase)
	{
		tryLocal();
		if (mFailed)
		{
			WriteLog (lsWARNING, LedgerAcquire) << " failed local for " << mHash;
		}
	}

	ripple::TMGetLedger tmGL;
	tmGL.set_ledgerhash(mHash.begin(), mHash.size());
	if (getTimeouts() != 0)
	{
		tmGL.set_querytype(ripple::qtINDIRECT);

		if (!isProgress() && !mFailed && mByHash && (getTimeouts() > LEDGER_TIMEOUT_AGGRESSIVE))
		{
			std::vector<neededHash_t> need = getNeededHashes();
			if (!need.empty())
			{
				ripple::TMGetObjectByHash tmBH;
				tmBH.set_query(true);
				tmBH.set_ledgerhash(mHash.begin(), mHash.size());
				bool typeSet = false;
				BOOST_FOREACH(neededHash_t& p, need)
				{
					WriteLog (lsWARNING, LedgerAcquire) << "Want: " << p.second;
					if (!typeSet)
					{
						tmBH.set_type(p.first);
						typeSet = true;
					}
					if (p.first == tmBH.type())
					{
						ripple::TMIndexedObject *io = tmBH.add_objects();
						io->set_hash(p.second.begin(), p.second.size());
					}
				}
				PackedMessage::pointer packet = boost::make_shared<PackedMessage>(tmBH, ripple::mtGET_OBJECTS);
				{
					boost::recursive_mutex::scoped_lock sl(mLock);
					for (boost::unordered_map<uint64, int>::iterator it = mPeers.begin(), end = mPeers.end();
						it != end; ++it)
					{
						Peer::pointer iPeer = theApp->getConnectionPool().getPeerById(it->first);
						if (iPeer)
						{
							mByHash = false;
							iPeer->sendPacket(packet, false);
						}
					}
				}
				WriteLog (lsINFO, LedgerAcquire) << "Attempting by hash fetch for ledger " << mHash;
			}
			else
			{
				WriteLog (lsINFO, LedgerAcquire) << "getNeededHashes says acquire is complete";
				mHaveBase = true;
				mHaveTransactions = true;
				mHaveState = true;
				mComplete = true;
			}
		}
	}

	if (!mHaveBase && !mFailed)
	{
		tmGL.set_itype(ripple::liBASE);
		WriteLog (lsTRACE, LedgerAcquire) << "Sending base request to " << (peer ? "selected peer" : "all peers");
		sendRequest(tmGL, peer);
		return;
	}

	if (mLedger)
		tmGL.set_ledgerseq(mLedger->getLedgerSeq());

	if (mHaveBase && !mHaveTransactions && !mFailed)
	{
		assert(mLedger);
		if (mLedger->peekTransactionMap()->getHash().isZero())
		{ // we need the root node
			tmGL.set_itype(ripple::liTX_NODE);
			*(tmGL.add_nodeids()) = SHAMapNode().getRawString();
			WriteLog (lsTRACE, LedgerAcquire) << "Sending TX root request to " << (peer ? "selected peer" : "all peers");
			sendRequest(tmGL, peer);
		}
		else
		{
			std::vector<SHAMapNode> nodeIDs;
			std::vector<uint256> nodeHashes;
			nodeIDs.reserve(256);
			nodeHashes.reserve(256);
			TransactionStateSF filter(mSeq);
			mLedger->peekTransactionMap()->getMissingNodes(nodeIDs, nodeHashes, 256, &filter);
			if (nodeIDs.empty())
			{
				if (!mLedger->peekTransactionMap()->isValid())
					mFailed = true;
				else
				{
					mHaveTransactions = true;
					if (mHaveState)
						mComplete = true;
				}
			}
			else
			{
				if (!mAggressive)
					filterNodes(nodeIDs, nodeHashes, mRecentTXNodes, 128, !isProgress());
				if (!nodeIDs.empty())
				{
					tmGL.set_itype(ripple::liTX_NODE);
					BOOST_FOREACH(SHAMapNode& it, nodeIDs)
					{
						*(tmGL.add_nodeids()) = it.getRawString();
					}
					WriteLog (lsTRACE, LedgerAcquire) << "Sending TX node " << nodeIDs.size()
						<< " request to " << (peer ? "selected peer" : "all peers");
					sendRequest(tmGL, peer);
				}
			}
		}
	}

	if (mHaveBase && !mHaveState && !mFailed)
	{
		assert(mLedger);
		if (mLedger->peekAccountStateMap()->getHash().isZero())
		{ // we need the root node
			tmGL.set_itype(ripple::liAS_NODE);
			*(tmGL.add_nodeids()) = SHAMapNode().getRawString();
			WriteLog (lsTRACE, LedgerAcquire) << "Sending AS root request to " << (peer ? "selected peer" : "all peers");
			sendRequest(tmGL, peer);
		}
		else
		{
			std::vector<SHAMapNode> nodeIDs;
			std::vector<uint256> nodeHashes;
			nodeIDs.reserve(256);
			nodeHashes.reserve(256);
			TransactionStateSF filter(mSeq);
			mLedger->peekAccountStateMap()->getMissingNodes(nodeIDs, nodeHashes, 256, &filter);
			if (nodeIDs.empty())
			{
				if (!mLedger->peekAccountStateMap()->isValid())
					mFailed = true;
				else
				{
					mHaveState = true;
					if (mHaveTransactions)
						mComplete = true;
				}
			}
			else
			{
				if (!mAggressive)
					filterNodes(nodeIDs, nodeHashes, mRecentASNodes, 128, !isProgress());
				if (!nodeIDs.empty())
				{
					tmGL.set_itype(ripple::liAS_NODE);
					BOOST_FOREACH(SHAMapNode& it, nodeIDs)
						*(tmGL.add_nodeids()) = it.getRawString();
					WriteLog (lsTRACE, LedgerAcquire) << "Sending AS node " << nodeIDs.size()
						<< " request to " << (peer ? "selected peer" : "all peers");
					CondLog (nodeIDs.size() == 1, lsTRACE, LedgerAcquire) << "AS node: " << nodeIDs[0];
					sendRequest(tmGL, peer);
				}
			}
		}
	}

	mRecentPeers.clear();

	if (mComplete || mFailed)
	{
		WriteLog (lsDEBUG, LedgerAcquire) << "Done:" << (mComplete ? " complete" : "") << (mFailed ? " failed " : " ")
			<< mLedger->getLedgerSeq();
		sl.unlock();
		done();
	}
}

void PeerSet::sendRequest(const ripple::TMGetLedger& tmGL, Peer::ref peer)
{
	if (!peer)
		sendRequest(tmGL);
	else
		peer->sendPacket(boost::make_shared<PackedMessage>(tmGL, ripple::mtGET_LEDGER), false);
}

void PeerSet::sendRequest(const ripple::TMGetLedger& tmGL)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (mPeers.empty())
		return;

	PackedMessage::pointer packet = boost::make_shared<PackedMessage>(tmGL, ripple::mtGET_LEDGER);
	for (boost::unordered_map<uint64, int>::iterator it = mPeers.begin(), end = mPeers.end(); it != end; ++it)
	{
		Peer::pointer peer = theApp->getConnectionPool().getPeerById(it->first);
		if (peer)
			peer->sendPacket(packet, false);
	}
}

int PeerSet::takePeerSetFrom(const PeerSet& s)
{
	int ret = 0;
	mPeers.clear();
	for (boost::unordered_map<uint64, int>::const_iterator it = s.mPeers.begin(), end = s.mPeers.end();
			it != end; ++it)
	{
		mPeers.insert(std::make_pair(it->first, 0));
		++ret;
	}
	return ret;
}

int PeerSet::getPeerCount() const
{
	int ret = 0;
	for (boost::unordered_map<uint64, int>::const_iterator it = mPeers.begin(), end = mPeers.end(); it != end; ++it)
		if (theApp->getConnectionPool().hasPeer(it->first))
			++ret;
	return ret;
}

void LedgerAcquire::filterNodes(std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& nodeHashes,
	std::set<SHAMapNode>& recentNodes, int max, bool aggressive)
{ // ask for new nodes in preference to ones we've already asked for
	assert(nodeIDs.size() == nodeHashes.size());

	std::vector<bool> duplicates;
	duplicates.reserve(nodeIDs.size());

	int dupCount = 0;
	for (unsigned int i = 0; i < nodeIDs.size(); ++i)
	{
		bool isDup = recentNodes.count(nodeIDs[i]) != 0;
		duplicates.push_back(isDup);
		if (isDup)
			++dupCount;
	}

	if (dupCount == nodeIDs.size())
	{ // all duplicates
		if (!aggressive)
		{
			nodeIDs.clear();
			nodeHashes.clear();
			return;
		}
	}
	else if (dupCount > 0)
	{ // some, but not all, duplicates
		int insertPoint = 0;
		for (unsigned int i = 0; i < nodeIDs.size(); ++i)
			if (!duplicates[i])
			{ // Keep this node
				if (insertPoint != i)
				{
					nodeIDs[insertPoint] = nodeIDs[i];
					nodeHashes[insertPoint] = nodeHashes[i];
				}
				++insertPoint;
			}
		WriteLog (lsDEBUG, LedgerAcquire) << "filterNodes " << nodeIDs.size() << " to " << insertPoint;
		nodeIDs.resize(insertPoint);
		nodeHashes.resize(insertPoint);
	}

	if (nodeIDs.size() > max)
	{
		nodeIDs.resize(max);
		nodeHashes.resize(max);
	}

	BOOST_FOREACH(const SHAMapNode& n, nodeIDs)
		recentNodes.insert(n);
}

bool LedgerAcquire::takeBase(const std::string& data) // data must not have hash prefix
{ // Return value: true=normal, false=bad data
#ifdef LA_DEBUG
	WriteLog (lsTRACE, LedgerAcquire) << "got base acquiring ledger " << mHash;
#endif
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (mComplete || mFailed || mHaveBase)
		return true;
	mLedger = boost::make_shared<Ledger>(data, false);
	if (mLedger->getHash() != mHash)
	{
		WriteLog (lsWARNING, LedgerAcquire) << "Acquire hash mismatch";
		WriteLog (lsWARNING, LedgerAcquire) << mLedger->getHash() << "!=" << mHash;
		mLedger.reset();
#ifdef TRUST_NETWORK
		assert(false);
#endif
		return false;
	}
	mHaveBase = true;

	Serializer s(data.size() + 4);
	s.add32(sHP_Ledger);
	s.addRaw(data);
	theApp->getHashedObjectStore().store(hotLEDGER, mLedger->getLedgerSeq(), s.peekData(), mHash);

	progress();
	if (!mLedger->getTransHash())
		mHaveTransactions = true;
	if (!mLedger->getAccountHash())
		mHaveState = true;
	mLedger->setAcquiring();
	return true;
}

bool LedgerAcquire::takeTxNode(const std::list<SHAMapNode>& nodeIDs,
	const std::list< std::vector<unsigned char> >& data, SMAddNode& san)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (!mHaveBase)
		return false;
	if (mHaveTransactions || mFailed)
		return true;

	std::list<SHAMapNode>::const_iterator nodeIDit = nodeIDs.begin();
	std::list< std::vector<unsigned char> >::const_iterator nodeDatait = data.begin();
	TransactionStateSF tFilter(mLedger->getLedgerSeq());
	while (nodeIDit != nodeIDs.end())
	{
		if (nodeIDit->isRoot())
		{
			if (!san.combine(mLedger->peekTransactionMap()->addRootNode(mLedger->getTransHash(), *nodeDatait,
				snfWIRE, &tFilter)))
				return false;
		}
		else
		{
			if (!san.combine(mLedger->peekTransactionMap()->addKnownNode(*nodeIDit, *nodeDatait, &tFilter)))
				return false;
		}
		++nodeIDit;
		++nodeDatait;
	}
	if (!mLedger->peekTransactionMap()->isSynching())
	{
		mHaveTransactions = true;
		if (mHaveState)
		{
			mComplete = true;
			done();
		}
	}
	progress();
	return true;
}

bool LedgerAcquire::takeAsNode(const std::list<SHAMapNode>& nodeIDs,
	const std::list< std::vector<unsigned char> >& data, SMAddNode& san)
{
	WriteLog (lsTRACE, LedgerAcquire) << "got ASdata (" << nodeIDs.size() <<") acquiring ledger " << mHash;
	CondLog (nodeIDs.size() == 1, lsTRACE, LedgerAcquire) << "got AS node: " << nodeIDs.front();

	boost::recursive_mutex::scoped_lock sl(mLock);
	if (!mHaveBase)
	{
		WriteLog (lsWARNING, LedgerAcquire) << "Don't have ledger base";
		return false;
	}
	if (mHaveState || mFailed)
		return true;

	std::list<SHAMapNode>::const_iterator nodeIDit = nodeIDs.begin();
	std::list< std::vector<unsigned char> >::const_iterator nodeDatait = data.begin();
	AccountStateSF tFilter(mLedger->getLedgerSeq());
	while (nodeIDit != nodeIDs.end())
	{
		if (nodeIDit->isRoot())
		{
			if (!san.combine(mLedger->peekAccountStateMap()->addRootNode(mLedger->getAccountHash(),
				*nodeDatait, snfWIRE, &tFilter)))
			{
				WriteLog (lsWARNING, LedgerAcquire) << "Bad ledger base";
				return false;
			}
		}
		else if (!san.combine(mLedger->peekAccountStateMap()->addKnownNode(*nodeIDit, *nodeDatait, &tFilter)))
		{
			WriteLog (lsWARNING, LedgerAcquire) << "Unable to add AS node";
			return false;
		}
		++nodeIDit;
		++nodeDatait;
	}
	if (!mLedger->peekAccountStateMap()->isSynching())
	{
		mHaveState = true;
		if (mHaveTransactions)
		{
			mComplete = true;
			done();
		}
	}
	progress();
	return true;
}

bool LedgerAcquire::takeAsRootNode(const std::vector<unsigned char>& data, SMAddNode& san)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (mFailed || mHaveState)
		return true;
	if (!mHaveBase)
		return false;
	AccountStateSF tFilter(mLedger->getLedgerSeq());
	return san.combine(
		mLedger->peekAccountStateMap()->addRootNode(mLedger->getAccountHash(), data, snfWIRE, &tFilter));
}

bool LedgerAcquire::takeTxRootNode(const std::vector<unsigned char>& data, SMAddNode& san)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (mFailed || mHaveState)
		return true;
	if (!mHaveBase)
		return false;
	TransactionStateSF tFilter(mLedger->getLedgerSeq());
	return san.combine(
		mLedger->peekTransactionMap()->addRootNode(mLedger->getTransHash(), data, snfWIRE, &tFilter));
}

LedgerAcquire::pointer LedgerAcquireMaster::findCreate(const uint256& hash, uint32 seq)
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

LedgerAcquire::pointer LedgerAcquireMaster::find(const uint256& hash)
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

std::vector<LedgerAcquire::neededHash_t> LedgerAcquire::getNeededHashes()
{
	std::vector<neededHash_t> ret;
	if (!mHaveBase)
	{
		ret.push_back(std::make_pair(ripple::TMGetObjectByHash::otLEDGER, mHash));
		return ret;
	}
	if (!mHaveState)
	{
		AccountStateSF filter(mLedger->getLedgerSeq());
		std::vector<uint256> v = mLedger->getNeededAccountStateHashes(4, &filter);
		BOOST_FOREACH(const uint256& h, v)
			ret.push_back(std::make_pair(ripple::TMGetObjectByHash::otSTATE_NODE, h));
	}
	if (!mHaveTransactions)
	{
		TransactionStateSF filter(mLedger->getLedgerSeq());
		std::vector<uint256> v = mLedger->getNeededAccountStateHashes(4, &filter);
		BOOST_FOREACH(const uint256& h, v)
			ret.push_back(std::make_pair(ripple::TMGetObjectByHash::otTRANSACTION_NODE, h));
	}
	return ret;
}

Json::Value LedgerAcquire::getJson(int)
{
	Json::Value ret(Json::objectValue);
	ret["hash"] = mHash.GetHex();
	if (mComplete)
		ret["complete"] = true;
	if (mFailed)
		ret["failed"] = true;
	ret["have_base"] = mHaveBase;
	ret["have_state"] = mHaveState;
	ret["have_transactions"] = mHaveTransactions;
	if (mAborted)
		ret["aborted"] = true;
	ret["timeouts"] = getTimeouts();
	if (mHaveBase && !mHaveState)
	{
		Json::Value hv(Json::arrayValue);
		std::vector<uint256> v = mLedger->peekAccountStateMap()->getNeededHashes(16, NULL);
		BOOST_FOREACH(const uint256& h, v)
			hv.append(h.GetHex());
		ret["needed_state_hashes"] = hv;
	}
	if (mHaveBase && !mHaveTransactions)
	{
		Json::Value hv(Json::arrayValue);
		std::vector<uint256> v = mLedger->peekTransactionMap()->getNeededHashes(16, NULL);
		BOOST_FOREACH(const uint256& h, v)
			hv.append(h.GetHex());
		ret["needed_transaction_hashes"] = hv;
	}
	return ret;
}

bool LedgerAcquireMaster::hasLedger(const uint256& hash)
{
	assert(hash.isNonZero());
	boost::mutex::scoped_lock sl(mLock);
	return mLedgers.find(hash) != mLedgers.end();
}

void LedgerAcquireMaster::dropLedger(const uint256& hash)
{
	assert(hash.isNonZero());
	boost::mutex::scoped_lock sl(mLock);
	mLedgers.erase(hash);
}

bool LedgerAcquireMaster::awaitLedgerData(const uint256& ledgerHash)
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
		SMAddNode san = SMAddNode::useful();
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
		std::list< std::vector<unsigned char> > nodeData;

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
			nodeData.push_back(std::vector<unsigned char>(node.nodedata().begin(), node.nodedata().end()));
		}
		SMAddNode ret;
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
