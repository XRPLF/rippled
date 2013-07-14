//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (NetworkOPs)

// This is the primary interface into the "client" portion of the program.
// Code that wants to do normal operations on the network such as
// creating and monitoring accounts, creating transactions, and so on
// should use this interface. The RPC code will primarily be a light wrapper
// over this code.

// Eventually, it will check the node's operating mode (synched, unsynched,
// etectera) and defer to the correct means of processing. The current
// code assumes this node is synched (and will continue to do so until
// there's a functional network.

NetworkOPs::NetworkOPs (LedgerMaster* pLedgerMaster)
    : mMode (omDISCONNECTED)
    , mNeedNetworkLedger (false)
    , mProposing (false)
    , mValidating (false)
    , mFeatureBlocked (false)
    , m_netTimer (this)
    , m_clusterTimer (this)
    , mLedgerMaster (pLedgerMaster)
    , mCloseTimeOffset (0)
    , mLastCloseProposers (0)
    , mLastCloseConvergeTime (1000 * LEDGER_IDLE_INTERVAL)
    , mLastCloseTime (0)
    , mLastValidationTime (0)
    , mFetchPack ("FetchPack", 2048, 20)
    , mLastFetchPack (0)
    // VFALCO TODO Give this magic number a name
    , mFetchSeq (static_cast <uint32> (-1))
    , mLastLoadBase (256)
    , mLastLoadFactor (256)
{
}

void NetworkOPs::processNetTimer ()
{
    ScopedLock sl (getApp().getMasterLock ());

    getApp().getLoadManager ().resetDeadlockDetector ();

    std::size_t const numPeers = getApp().getPeers ().getPeerVector ().size ();

    // do we have sufficient peers? If not, we are disconnected.
    if (numPeers < theConfig.NETWORK_QUORUM)
    {
        if (mMode != omDISCONNECTED)
        {
            setMode (omDISCONNECTED);
            WriteLog (lsWARNING, NetworkOPs)
                << "Node count (" << numPeers << ") "
                << "has fallen below quorum (" << theConfig.NETWORK_QUORUM << ").";
        }

        return;
    }

    if (mMode == omDISCONNECTED)
    {
        setMode (omCONNECTED);
        WriteLog (lsINFO, NetworkOPs) << "Node count (" << numPeers << ") is sufficient.";
    }

    // Check if the last validated ledger forces a change between these states
    if (mMode == omSYNCING)
    {
        setMode (omSYNCING);
    }
    else if (mMode == omCONNECTED)
    {
        setMode (omCONNECTED);
    }

    if (!mConsensus)
        tryStartConsensus ();

    if (mConsensus)
        mConsensus->timerEntry ();
}

void NetworkOPs::onDeadlineTimer (DeadlineTimer& timer)
{
    if (timer == m_netTimer)
    {
        processNetTimer ();
    }
    else if (timer == m_clusterTimer)
    {
        doClusterReport();
    }
}

std::string NetworkOPs::strOperatingMode ()
{
    static const char* paStatusToken [] =
    {
        "disconnected",
        "connected",
        "syncing",
        "tracking",
        "full"
    };

    if (mMode == omFULL)
    {
        if (mProposing)
            return "proposing";

        if (mValidating)
            return "validating";
    }

    return paStatusToken[mMode];
}

boost::posix_time::ptime NetworkOPs::getNetworkTimePT ()
{
    int offset = 0;

    getApp().getSystemTimeOffset (offset);

    // VFALCO TODO Replace this with a beast call
    return boost::posix_time::microsec_clock::universal_time () + boost::posix_time::seconds (offset);
}

uint32 NetworkOPs::getNetworkTimeNC ()
{
    return iToSeconds (getNetworkTimePT ());
}

uint32 NetworkOPs::getCloseTimeNC ()
{
    return iToSeconds (getNetworkTimePT () + boost::posix_time::seconds (mCloseTimeOffset));
}

uint32 NetworkOPs::getValidationTimeNC ()
{
    uint32 vt = getNetworkTimeNC ();

    if (vt <= mLastValidationTime)
        vt = mLastValidationTime + 1;

    mLastValidationTime = vt;
    return vt;
}

void NetworkOPs::closeTimeOffset (int offset)
{
    // take large offsets, ignore small offsets, push towards our wall time
    if (offset > 1)
        mCloseTimeOffset += (offset + 3) / 4;
    else if (offset < -1)
        mCloseTimeOffset += (offset - 3) / 4;
    else
        mCloseTimeOffset = (mCloseTimeOffset * 3) / 4;

    CondLog (mCloseTimeOffset != 0, lsINFO, NetworkOPs) << "Close time offset now " << mCloseTimeOffset;
}

uint32 NetworkOPs::getLedgerID (uint256 const& hash)
{
    Ledger::pointer  lrLedger   = mLedgerMaster->getLedgerByHash (hash);

    return lrLedger ? lrLedger->getLedgerSeq () : 0;
}

Ledger::pointer NetworkOPs::getLedgerBySeq (const uint32 seq)
{
    return mLedgerMaster->getLedgerBySeq (seq);
}

uint32 NetworkOPs::getCurrentLedgerID ()
{
    return mLedgerMaster->getCurrentLedger ()->getLedgerSeq ();
}

bool NetworkOPs::haveLedgerRange (uint32 from, uint32 to)
{
    return mLedgerMaster->haveLedgerRange (from, to);
}

bool NetworkOPs::haveLedger (uint32 seq)
{
    return mLedgerMaster->haveLedger (seq);
}

uint32 NetworkOPs::getValidatedSeq ()
{
    return mLedgerMaster->getValidatedLedger ()->getLedgerSeq ();
}

bool NetworkOPs::isValidated (uint32 seq, uint256 const& hash)
{
    if (!isValidated (seq))
        return false;

    return mLedgerMaster->getHashBySeq (seq) == hash;
}

bool NetworkOPs::isValidated (uint32 seq)
{
    // use when ledger was retrieved by seq
    return haveLedger (seq) && (seq <= mLedgerMaster->getValidatedLedger ()->getLedgerSeq ());
}

void NetworkOPs::submitTransaction (Job&, SerializedTransaction::pointer iTrans, stCallback callback)
{
    // this is an asynchronous interface
    Serializer s;
    iTrans->add (s);

    SerializerIterator sit (s);
    SerializedTransaction::pointer trans = boost::make_shared<SerializedTransaction> (boost::ref (sit));

    uint256 suppress = trans->getTransactionID ();
    int flags;

    if (getApp().getHashRouter ().addSuppressionPeer (suppress, 0, flags) && ((flags & SF_RETRY) != 0))
    {
        WriteLog (lsWARNING, NetworkOPs) << "Redundant transactions submitted";
        return;
    }

    if ((flags & SF_BAD) != 0)
    {
        WriteLog (lsWARNING, NetworkOPs) << "Submitted transaction cached bad";
        return;
    }

    if ((flags & SF_SIGGOOD) == 0)
    {
        try
        {
            if (!trans->checkSign ())
            {
                WriteLog (lsWARNING, NetworkOPs) << "Submitted transaction has bad signature";
                getApp().getHashRouter ().setFlag (suppress, SF_BAD);
                return;
            }

            getApp().getHashRouter ().setFlag (suppress, SF_SIGGOOD);
        }
        catch (...)
        {
            WriteLog (lsWARNING, NetworkOPs) << "Exception checking transaction " << suppress;
            return;
        }
    }

    // FIXME: Should submit to job queue
    getApp().getIOService ().post (boost::bind (&NetworkOPs::processTransaction, this,
                                  boost::make_shared<Transaction> (trans, false), false, false, callback));
}

// Sterilize transaction through serialization.
// This is fully synchronous and deprecated
Transaction::pointer NetworkOPs::submitTransactionSync (Transaction::ref tpTrans, bool bAdmin, bool bFailHard, bool bSubmit)
{
    Serializer s;
    tpTrans->getSTransaction ()->add (s);

    Transaction::pointer    tpTransNew  = Transaction::sharedTransaction (s.getData (), true);

    if (!tpTransNew)
    {
        // Could not construct transaction.
        nothing ();
    }
    else if (tpTransNew->getSTransaction ()->isEquivalent (*tpTrans->getSTransaction ()))
    {
        if (bSubmit)
            (void) NetworkOPs::processTransaction (tpTransNew, bAdmin, bFailHard);
    }
    else
    {
        WriteLog (lsFATAL, NetworkOPs) << "Transaction reconstruction failure";
        WriteLog (lsFATAL, NetworkOPs) << tpTransNew->getSTransaction ()->getJson (0);
        WriteLog (lsFATAL, NetworkOPs) << tpTrans->getSTransaction ()->getJson (0);

        // assert (false); "1e-95" as amount can trigger this

        tpTransNew.reset ();
    }

    return tpTransNew;
}

void NetworkOPs::runTransactionQueue ()
{
    TXQEntry::pointer txn;

    for (int i = 0; i < 10; ++i)
    {
        getApp().getTxnQueue ().getJob (txn);

        if (!txn)
            return;

        {
            LoadEvent::autoptr ev = getApp().getJobQueue ().getLoadEventAP (jtTXN_PROC, "runTxnQ");

            boost::recursive_mutex::scoped_lock sl (getApp().getMasterLock ());

            Transaction::pointer dbtx = getApp().getMasterTransaction ().fetch (txn->getID (), true);
            assert (dbtx);

            bool didApply;
            TER r = mLedgerMaster->doTransaction (dbtx->getSTransaction (),
                                                  tapOPEN_LEDGER | tapNO_CHECK_SIGN, didApply);
            dbtx->setResult (r);

            if (isTemMalformed (r)) // malformed, cache bad
                getApp().getHashRouter ().setFlag (txn->getID (), SF_BAD);
//            else if (isTelLocal (r) || isTerRetry (r)) // can be retried
//                getApp().getHashRouter ().setFlag (txn->getID (), SF_RETRY);


            if (isTerRetry (r))
            {
                // transaction should be held
                WriteLog (lsDEBUG, NetworkOPs) << "QTransaction should be held: " << r;
                dbtx->setStatus (HELD);
                getApp().getMasterTransaction ().canonicalize (dbtx);
                mLedgerMaster->addHeldTransaction (dbtx);
            }
            else if (r == tefPAST_SEQ)
            {
                // duplicate or conflict
                WriteLog (lsINFO, NetworkOPs) << "QTransaction is obsolete";
                dbtx->setStatus (OBSOLETE);
            }
            else if (r == tesSUCCESS)
            {
                WriteLog (lsINFO, NetworkOPs) << "QTransaction is now included in open ledger";
                dbtx->setStatus (INCLUDED);
                getApp().getMasterTransaction ().canonicalize (dbtx);
            }
            else
            {
                WriteLog (lsDEBUG, NetworkOPs) << "QStatus other than success " << r;
                dbtx->setStatus (INVALID);
            }

//            if (didApply || (mMode != omFULL))
            if (didApply)
            {
                std::set<uint64> peers;

                if (getApp().getHashRouter ().swapSet (txn->getID (), peers, SF_RELAYED))
                {
                    WriteLog (lsDEBUG, NetworkOPs) << "relaying";
                    protocol::TMTransaction tx;
                    Serializer s;
                    dbtx->getSTransaction ()->add (s);
                    tx.set_rawtransaction (&s.getData ().front (), s.getLength ());
                    tx.set_status (protocol::tsCURRENT);
                    tx.set_receivetimestamp (getNetworkTimeNC ()); // FIXME: This should be when we received it

                    PackedMessage::pointer packet = boost::make_shared<PackedMessage> (tx, protocol::mtTRANSACTION);
                    getApp().getPeers ().relayMessageBut (peers, packet);
                }
                else
                    WriteLog(lsDEBUG, NetworkOPs) << "recently relayed";
            }

            txn->doCallbacks (r);
        }
    }

    if (getApp().getTxnQueue ().stopProcessing (txn))
        getApp().getIOService ().post (BIND_TYPE (&NetworkOPs::runTransactionQueue, this));
}

Transaction::pointer NetworkOPs::processTransaction (Transaction::pointer trans, bool bAdmin, bool bFailHard, stCallback callback)
{
    LoadEvent::autoptr ev = getApp().getJobQueue ().getLoadEventAP (jtTXN_PROC, "ProcessTXN");

    int newFlags = getApp().getHashRouter ().getFlags (trans->getID ());

    if ((newFlags & SF_BAD) != 0)
    {
        // cached bad
        trans->setStatus (INVALID);
        trans->setResult (temBAD_SIGNATURE);
        return trans;
    }

    if ((newFlags & SF_SIGGOOD) == 0)
    {
        // signature not checked
        if (!trans->checkSign ())
        {
            WriteLog (lsINFO, NetworkOPs) << "Transaction has bad signature";
            trans->setStatus (INVALID);
            trans->setResult (temBAD_SIGNATURE);
            getApp().getHashRouter ().setFlag (trans->getID (), SF_BAD);
            return trans;
        }

        getApp().getHashRouter ().setFlag (trans->getID (), SF_SIGGOOD);
    }

    boost::recursive_mutex::scoped_lock sl (getApp().getMasterLock ());
    bool didApply;
    TER r = mLedgerMaster->doTransaction (trans->getSTransaction (),
                                          bAdmin ? (tapOPEN_LEDGER | tapNO_CHECK_SIGN | tapADMIN) : (tapOPEN_LEDGER | tapNO_CHECK_SIGN), didApply);
    trans->setResult (r);

    if (isTemMalformed (r)) // malformed, cache bad
        getApp().getHashRouter ().setFlag (trans->getID (), SF_BAD);
//    else if (isTelLocal (r) || isTerRetry (r)) // can be retried
//        getApp().getHashRouter ().setFlag (trans->getID (), SF_RETRY);

#ifdef BEAST_DEBUG

    if (r != tesSUCCESS)
    {
        std::string token, human;
        CondLog (transResultInfo (r, token, human), lsINFO, NetworkOPs) << "TransactionResult: " << token << ": " << human;
    }

#endif

    if (callback)
        callback (trans, r);

    if (r == tefFAILURE)
        throw Fault (IO_ERROR);

    if (r == tesSUCCESS)
    {
        WriteLog (lsINFO, NetworkOPs) << "Transaction is now included in open ledger";
        trans->setStatus (INCLUDED);
        getApp().getMasterTransaction ().canonicalize (trans);
    }
    else if (r == tefPAST_SEQ)
    {
        // duplicate or conflict
        WriteLog (lsINFO, NetworkOPs) << "Transaction is obsolete";
        trans->setStatus (OBSOLETE);
    }
    else if (isTerRetry (r))
    {
        if (!bFailHard)
        {
                // transaction should be held
                WriteLog (lsDEBUG, NetworkOPs) << "Transaction should be held: " << r;
                trans->setStatus (HELD);
                getApp().getMasterTransaction ().canonicalize (trans);
                mLedgerMaster->addHeldTransaction (trans);
        }
    }
    else
    {
        WriteLog (lsDEBUG, NetworkOPs) << "Status other than success " << r;
        trans->setStatus (INVALID);
    }

    if (didApply || ((mMode != omFULL) && !bFailHard))
    {
        std::set<uint64> peers;

        if (getApp().getHashRouter ().swapSet (trans->getID (), peers, SF_RELAYED))
        {
            protocol::TMTransaction tx;
            Serializer s;
            trans->getSTransaction ()->add (s);
            tx.set_rawtransaction (&s.getData ().front (), s.getLength ());
            tx.set_status (protocol::tsCURRENT);
            tx.set_receivetimestamp (getNetworkTimeNC ()); // FIXME: This should be when we received it

            PackedMessage::pointer packet = boost::make_shared<PackedMessage> (tx, protocol::mtTRANSACTION);
            getApp().getPeers ().relayMessageBut (peers, packet);
        }
    }

    return trans;
}

Transaction::pointer NetworkOPs::findTransactionByID (uint256 const& transactionID)
{
    return Transaction::load (transactionID);
}

int NetworkOPs::findTransactionsByDestination (std::list<Transaction::pointer>& txns,
        const RippleAddress& destinationAccount, uint32 startLedgerSeq, uint32 endLedgerSeq, int maxTransactions)
{
    // WRITEME
    return 0;
}

//
// Account functions
//

AccountState::pointer NetworkOPs::getAccountState (Ledger::ref lrLedger, const RippleAddress& accountID)
{
    return lrLedger->getAccountState (accountID);
}

SLE::pointer NetworkOPs::getGenerator (Ledger::ref lrLedger, const uint160& uGeneratorID)
{
    if (!lrLedger)
        return SLE::pointer ();

    return lrLedger->getGenerator (uGeneratorID);
}

//
// Directory functions
//

// <-- false : no entrieS
STVector256 NetworkOPs::getDirNodeInfo (
    Ledger::ref         lrLedger,
    uint256 const&      uNodeIndex,
    uint64&             uNodePrevious,
    uint64&             uNodeNext)
{
    STVector256         svIndexes;
    SLE::pointer        sleNode     = lrLedger->getDirNode (uNodeIndex);

    if (sleNode)
    {
        WriteLog (lsDEBUG, NetworkOPs) << "getDirNodeInfo: node index: " << uNodeIndex.ToString ();

        WriteLog (lsTRACE, NetworkOPs) << "getDirNodeInfo: first: " << strHex (sleNode->getFieldU64 (sfIndexPrevious));
        WriteLog (lsTRACE, NetworkOPs) << "getDirNodeInfo:  last: " << strHex (sleNode->getFieldU64 (sfIndexNext));

        uNodePrevious   = sleNode->getFieldU64 (sfIndexPrevious);
        uNodeNext       = sleNode->getFieldU64 (sfIndexNext);
        svIndexes       = sleNode->getFieldV256 (sfIndexes);

        WriteLog (lsTRACE, NetworkOPs) << "getDirNodeInfo: first: " << strHex (uNodePrevious);
        WriteLog (lsTRACE, NetworkOPs) << "getDirNodeInfo:  last: " << strHex (uNodeNext);
    }
    else
    {
        WriteLog (lsINFO, NetworkOPs) << "getDirNodeInfo: node index: NOT FOUND: " << uNodeIndex.ToString ();

        uNodePrevious   = 0;
        uNodeNext       = 0;
    }

    return svIndexes;
}

#if 0
//
// Nickname functions
//

NicknameState::pointer NetworkOPs::getNicknameState (uint256 const& uLedger, const std::string& strNickname)
{
    return mLedgerMaster->getLedgerByHash (uLedger)->getNicknameState (strNickname);
}
#endif

//
// Owner functions
//

Json::Value NetworkOPs::getOwnerInfo (Ledger::pointer lpLedger, const RippleAddress& naAccount)
{
    Json::Value jvObjects (Json::objectValue);

    uint256             uRootIndex  = lpLedger->getOwnerDirIndex (naAccount.getAccountID ());

    SLE::pointer        sleNode     = lpLedger->getDirNode (uRootIndex);

    if (sleNode)
    {
        uint64  uNodeDir;

        do
        {
            STVector256                 svIndexes   = sleNode->getFieldV256 (sfIndexes);
            const std::vector<uint256>& vuiIndexes  = svIndexes.peekValue ();

            BOOST_FOREACH (uint256 const & uDirEntry, vuiIndexes)
            {
                SLE::pointer        sleCur      = lpLedger->getSLEi (uDirEntry);

                switch (sleCur->getType ())
                {
                case ltOFFER:
                    if (!jvObjects.isMember ("offers"))
                        jvObjects["offers"]         = Json::Value (Json::arrayValue);

                    jvObjects["offers"].append (sleCur->getJson (0));
                    break;

                case ltRIPPLE_STATE:
                    if (!jvObjects.isMember ("ripple_lines"))
                        jvObjects["ripple_lines"]   = Json::Value (Json::arrayValue);

                    jvObjects["ripple_lines"].append (sleCur->getJson (0));
                    break;

                case ltACCOUNT_ROOT:
                case ltDIR_NODE:
                case ltGENERATOR_MAP:
                case ltNICKNAME:
                default:
                    assert (false);
                    break;
                }
            }

            uNodeDir        = sleNode->getFieldU64 (sfIndexNext);

            if (uNodeDir)
            {
                sleNode = lpLedger->getDirNode (Ledger::getDirNodeIndex (uRootIndex, uNodeDir));
                assert (sleNode);
            }
        }
        while (uNodeDir);
    }

    return jvObjects;
}

//
// Other
//

void NetworkOPs::setFeatureBlocked ()
{
    mFeatureBlocked = true;
    setMode (omTRACKING);
}

void NetworkOPs::setStateTimer ()
{
    m_netTimer.setRecurringExpiration (LEDGER_GRANULARITY / 1000.0);

    m_clusterTimer.setRecurringExpiration (10.0);
}

class ValidationCount
{
public:
    int trustedValidations, nodesUsing;
    uint160 highNodeUsing, highValidation;

    ValidationCount () : trustedValidations (0), nodesUsing (0)
    {
    }

    bool operator> (const ValidationCount& v)
    {
        if (trustedValidations > v.trustedValidations)
            return true;

        if (trustedValidations < v.trustedValidations)
            return false;

        if (trustedValidations == 0)
        {
            if (nodesUsing > v.nodesUsing)
                return true;

            if (nodesUsing < v.nodesUsing) return
                false;

            return highNodeUsing > v.highNodeUsing;
        }

        return highValidation > v.highValidation;
    }
};

void NetworkOPs::tryStartConsensus ()
{
    uint256 networkClosed;
    bool ledgerChange = checkLastClosedLedger (getApp().getPeers ().getPeerVector (), networkClosed);

    if (networkClosed.isZero ())
        return;

    // WRITEME: Unless we are in omFULL and in the process of doing a consensus,
    // we must count how many nodes share our LCL, how many nodes disagree with our LCL,
    // and how many validations our LCL has. We also want to check timing to make sure
    // there shouldn't be a newer LCL. We need this information to do the next three
    // tests.

    if (((mMode == omCONNECTED) || (mMode == omSYNCING)) && !ledgerChange)
    {
        // count number of peers that agree with us and UNL nodes whose validations we have for LCL
        // if the ledger is good enough, go to omTRACKING - TODO
        if (!mNeedNetworkLedger)
            setMode (omTRACKING);
    }

    if (((mMode == omCONNECTED) || (mMode == omTRACKING)) && !ledgerChange)
    {
        // check if the ledger is good enough to go to omFULL
        // Note: Do not go to omFULL if we don't have the previous ledger
        // check if the ledger is bad enough to go to omCONNECTED -- TODO
        if (getApp().getOPs ().getNetworkTimeNC () < mLedgerMaster->getCurrentLedger ()->getCloseTimeNC ())
            setMode (omFULL);
    }

    if ((!mConsensus) && (mMode != omDISCONNECTED))
        beginConsensus (networkClosed, mLedgerMaster->getCurrentLedger ());
}

bool NetworkOPs::checkLastClosedLedger (const std::vector<Peer::pointer>& peerList, uint256& networkClosed)
{
    // Returns true if there's an *abnormal* ledger issue, normal changing in TRACKING mode should return false
    // Do we have sufficient validations for our last closed ledger? Or do sufficient nodes
    // agree? And do we have no better ledger available?
    // If so, we are either tracking or full.

    WriteLog (lsTRACE, NetworkOPs) << "NetworkOPs::checkLastClosedLedger";

    Ledger::pointer ourClosed = mLedgerMaster->getClosedLedger ();

    if (!ourClosed)
        return false;

    uint256 closedLedger = ourClosed->getHash ();
    uint256 prevClosedLedger = ourClosed->getParentHash ();
    WriteLog (lsTRACE, NetworkOPs) << "OurClosed:  " << closedLedger;
    WriteLog (lsTRACE, NetworkOPs) << "PrevClosed: " << prevClosedLedger;

    boost::unordered_map<uint256, ValidationCount> ledgers;
    {
        boost::unordered_map<uint256, currentValidationCount> current =
            getApp().getValidations ().getCurrentValidations (closedLedger, prevClosedLedger);
        typedef std::map<uint256, currentValidationCount>::value_type u256_cvc_pair;
        BOOST_FOREACH (const u256_cvc_pair & it, current)
        {
            ValidationCount& vc = ledgers[it.first];
            vc.trustedValidations += it.second.first;

            if (it.second.second > vc.highValidation)
                vc.highValidation = it.second.second;
        }
    }

    ValidationCount& ourVC = ledgers[closedLedger];

    if (mMode >= omTRACKING)
    {
        ++ourVC.nodesUsing;
        uint160 ourAddress = getApp().getLocalCredentials ().getNodePublic ().getNodeID ();

        if (ourAddress > ourVC.highNodeUsing)
            ourVC.highNodeUsing = ourAddress;
    }

    BOOST_FOREACH (Peer::ref it, peerList)
    {
        if (it && it->isConnected ())
        {
            uint256 peerLedger = it->getClosedLedgerHash ();

            if (peerLedger.isNonZero ())
            {
                ValidationCount& vc = ledgers[peerLedger];

                if ((vc.nodesUsing == 0) || (it->getNodePublic ().getNodeID () > vc.highNodeUsing))
                    vc.highNodeUsing = it->getNodePublic ().getNodeID ();

                ++vc.nodesUsing;
            }
        }
    }

    ValidationCount bestVC = ledgers[closedLedger];

    // 3) Is there a network ledger we'd like to switch to? If so, do we have it?
    bool switchLedgers = false;

    for (boost::unordered_map<uint256, ValidationCount>::iterator it = ledgers.begin (), end = ledgers.end ();
            it != end; ++it)
    {
        WriteLog (lsDEBUG, NetworkOPs) << "L: " << it->first << " t=" << it->second.trustedValidations <<
                                       ", n=" << it->second.nodesUsing;

        // Temporary logging to make sure tiebreaking isn't broken
        if (it->second.trustedValidations > 0)
            WriteLog (lsTRACE, NetworkOPs) << "  TieBreakTV: " << it->second.highValidation;
        else
            CondLog (it->second.nodesUsing > 0, lsTRACE, NetworkOPs) << "  TieBreakNU: " << it->second.highNodeUsing;

        if (it->second > bestVC)
        {
            bestVC = it->second;
            closedLedger = it->first;
            switchLedgers = true;
        }
    }

    if (switchLedgers && (closedLedger == prevClosedLedger))
    {
        // don't switch to our own previous ledger
        WriteLog (lsINFO, NetworkOPs) << "We won't switch to our own previous ledger";
        networkClosed = ourClosed->getHash ();
        switchLedgers = false;
    }
    else
        networkClosed = closedLedger;

    if (!switchLedgers)
    {
        if (mAcquiringLedger)
        {
            mAcquiringLedger->abort ();
            getApp().getInboundLedgers ().dropLedger (mAcquiringLedger->getHash ());
            mAcquiringLedger.reset ();
        }

        return false;
    }

    WriteLog (lsWARNING, NetworkOPs) << "We are not running on the consensus ledger";
    WriteLog (lsINFO, NetworkOPs) << "Our LCL: " << ourClosed->getJson (0);
    WriteLog (lsINFO, NetworkOPs) << "Net LCL " << closedLedger;

    if ((mMode == omTRACKING) || (mMode == omFULL))
        setMode (omCONNECTED);

    Ledger::pointer consensus = mLedgerMaster->getLedgerByHash (closedLedger);

    if (!consensus)
    {
        WriteLog (lsINFO, NetworkOPs) << "Acquiring consensus ledger " << closedLedger;

        if (!mAcquiringLedger || (mAcquiringLedger->getHash () != closedLedger))
            mAcquiringLedger = getApp().getInboundLedgers ().findCreate (closedLedger, 0);

        if (!mAcquiringLedger || mAcquiringLedger->isFailed ())
        {
            getApp().getInboundLedgers ().dropLedger (closedLedger);
            WriteLog (lsERROR, NetworkOPs) << "Network ledger cannot be acquired";
            return true;
        }

        if (!mAcquiringLedger->isComplete ())
            return true;

        clearNeedNetworkLedger ();
        consensus = mAcquiringLedger->getLedger ();
    }

    // FIXME: If this rewinds the ledger sequence, or has the same sequence, we should update the status on
    // any stored transactions in the invalidated ledgers.
    switchLastClosedLedger (consensus, false);

    return true;
}

void NetworkOPs::switchLastClosedLedger (Ledger::pointer newLedger, bool duringConsensus)
{
    // set the newledger as our last closed ledger -- this is abnormal code

    if (duringConsensus)
        WriteLog (lsERROR, NetworkOPs) << "JUMPdc last closed ledger to " << newLedger->getHash ();
    else
        WriteLog (lsERROR, NetworkOPs) << "JUMP last closed ledger to " << newLedger->getHash ();

    clearNeedNetworkLedger ();
    newLedger->setClosed ();
    Ledger::pointer openLedger = boost::make_shared<Ledger> (false, boost::ref (*newLedger));
    mLedgerMaster->switchLedgers (newLedger, openLedger);

    protocol::TMStatusChange s;
    s.set_newevent (protocol::neSWITCHED_LEDGER);
    s.set_ledgerseq (newLedger->getLedgerSeq ());
    s.set_networktime (getApp().getOPs ().getNetworkTimeNC ());
    uint256 hash = newLedger->getParentHash ();
    s.set_ledgerhashprevious (hash.begin (), hash.size ());
    hash = newLedger->getHash ();
    s.set_ledgerhash (hash.begin (), hash.size ());
    PackedMessage::pointer packet = boost::make_shared<PackedMessage> (s, protocol::mtSTATUS_CHANGE);
    getApp().getPeers ().relayMessage (NULL, packet);
}

int NetworkOPs::beginConsensus (uint256 const& networkClosed, Ledger::pointer closingLedger)
{
    WriteLog (lsINFO, NetworkOPs) << "Consensus time for ledger " << closingLedger->getLedgerSeq ();
    WriteLog (lsINFO, NetworkOPs) << " LCL is " << closingLedger->getParentHash ();

    Ledger::pointer prevLedger = mLedgerMaster->getLedgerByHash (closingLedger->getParentHash ());

    if (!prevLedger)
    {
        // this shouldn't happen unless we jump ledgers
        if (mMode == omFULL)
        {
            WriteLog (lsWARNING, NetworkOPs) << "Don't have LCL, going to tracking";
            setMode (omTRACKING);
        }

        return 3;
    }

    assert (prevLedger->getHash () == closingLedger->getParentHash ());
    assert (closingLedger->getParentHash () == mLedgerMaster->getClosedLedger ()->getHash ());

    // Create a consensus object to get consensus on this ledger
    assert (!mConsensus);
    prevLedger->setImmutable ();
    mConsensus = boost::make_shared<LedgerConsensus> (
                     networkClosed, prevLedger, mLedgerMaster->getCurrentLedger ()->getCloseTimeNC ());

    WriteLog (lsDEBUG, NetworkOPs) << "Initiating consensus engine";
    return mConsensus->startup ();
}

bool NetworkOPs::haveConsensusObject ()
{
    if (mConsensus != nullptr)
        return true;

    if ((mMode == omFULL) || (mMode == omTRACKING))
    {
        tryStartConsensus ();
    }
    else
    {
        // we need to get into the consensus process
        uint256 networkClosed;
        std::vector<Peer::pointer> peerList = getApp().getPeers ().getPeerVector ();
        bool ledgerChange = checkLastClosedLedger (peerList, networkClosed);

        if (!ledgerChange)
        {
            WriteLog (lsINFO, NetworkOPs) << "Beginning consensus due to peer action";
            beginConsensus (networkClosed, mLedgerMaster->getCurrentLedger ());
        }
    }

    return mConsensus != nullptr;
}

uint256 NetworkOPs::getConsensusLCL ()
{
    if (!haveConsensusObject ())
        return uint256 ();

    return mConsensus->getLCL ();
}

void NetworkOPs::processTrustedProposal (LedgerProposal::pointer proposal,
        boost::shared_ptr<protocol::TMProposeSet> set, RippleAddress nodePublic, uint256 checkLedger, bool sigGood)
{
    boost::recursive_mutex::scoped_lock sl (getApp().getMasterLock ());

    bool relay = true;

    if (!haveConsensusObject ())
    {
        WriteLog (lsINFO, NetworkOPs) << "Received proposal outside consensus window";

        if (mMode == omFULL)
            relay = false;
    }
    else
    {
        storeProposal (proposal, nodePublic);

        uint256 consensusLCL = mConsensus->getLCL ();

        if (!set->has_previousledger () && (checkLedger != consensusLCL))
        {
            WriteLog (lsWARNING, NetworkOPs) << "Have to re-check proposal signature due to consensus view change";
            assert (proposal->hasSignature ());
            proposal->setPrevLedger (consensusLCL);

            if (proposal->checkSign ())
                sigGood = true;
        }

        if (sigGood && (consensusLCL == proposal->getPrevLedger ()))
        {
            relay = mConsensus->peerPosition (proposal);
            WriteLog (lsTRACE, NetworkOPs) << "Proposal processing finished, relay=" << relay;
        }
    }

    if (relay)
    {
        std::set<uint64> peers;
        getApp().getHashRouter ().swapSet (proposal->getHashRouter (), peers, SF_RELAYED);
        PackedMessage::pointer message = boost::make_shared<PackedMessage> (*set, protocol::mtPROPOSE_LEDGER);
        getApp().getPeers ().relayMessageBut (peers, message);
    }
    else
        WriteLog (lsINFO, NetworkOPs) << "Not relaying trusted proposal";
}

SHAMap::pointer NetworkOPs::getTXMap (uint256 const& hash)
{
    std::map<uint256, std::pair<int, SHAMap::pointer> >::iterator it = mRecentPositions.find (hash);

    if (it != mRecentPositions.end ())
        return it->second.second;

    if (!haveConsensusObject ())
        return SHAMap::pointer ();

    return mConsensus->getTransactionTree (hash, false);
}

void NetworkOPs::takePosition (int seq, SHAMap::ref position)
{
    mRecentPositions[position->getHash ()] = std::make_pair (seq, position);

    if (mRecentPositions.size () > 4)
    {
        std::map<uint256, std::pair<int, SHAMap::pointer> >::iterator it = mRecentPositions.begin ();

        while (it != mRecentPositions.end ())
        {
            if (it->second.first < (seq - 2))
            {
                mRecentPositions.erase (it);
                return;
            }

            ++it;
        }
    }
}

SHAMapAddNode NetworkOPs::gotTXData (const boost::shared_ptr<Peer>& peer, uint256 const& hash,
                                     const std::list<SHAMapNode>& nodeIDs, const std::list< Blob >& nodeData)
{

    boost::shared_ptr<LedgerConsensus> consensus;
    {
        ScopedLock mlh(getApp().getMasterLock());
        consensus = mConsensus;
    }

    if (!consensus)
    {
        WriteLog (lsWARNING, NetworkOPs) << "Got TX data with no consensus object";
        return SHAMapAddNode ();
    }

    return consensus->peerGaveNodes (peer, hash, nodeIDs, nodeData);
}

bool NetworkOPs::hasTXSet (const boost::shared_ptr<Peer>& peer, uint256 const& set, protocol::TxSetStatus status)
{
    if (!haveConsensusObject ())
    {
        WriteLog (lsINFO, NetworkOPs) << "Peer has TX set, not during consensus";
        return false;
    }

    return mConsensus->peerHasSet (peer, set, status);
}

bool NetworkOPs::stillNeedTXSet (uint256 const& hash)
{
    if (!mConsensus)
        return false;

    return mConsensus->stillNeedTXSet (hash);
}

void NetworkOPs::mapComplete (uint256 const& hash, SHAMap::ref map)
{
    if (haveConsensusObject ())
        mConsensus->mapComplete (hash, map, true);
}

void NetworkOPs::endConsensus (bool correctLCL)
{
    uint256 deadLedger = mLedgerMaster->getClosedLedger ()->getParentHash ();

    std::vector <Peer::pointer> peerList = getApp().getPeers ().getPeerVector ();

    BOOST_FOREACH (Peer::ref it, peerList)
    {
        if (it && (it->getClosedLedgerHash () == deadLedger))
        {
            WriteLog (lsTRACE, NetworkOPs) << "Killing obsolete peer status";
            it->cycleStatus ();
        }
    }

    mConsensus = boost::shared_ptr<LedgerConsensus> ();
}

void NetworkOPs::consensusViewChange ()
{
    if ((mMode == omFULL) || (mMode == omTRACKING))
        setMode (omCONNECTED);
}

void NetworkOPs::pubServer ()
{
    // VFALCO TODO Don't hold the lock across calls to send...make a copy of the
    //             list into a local array while holding the lock then release the
    //             lock and call send on everyone.
    //
    boost::recursive_mutex::scoped_lock sl (mMonitorLock);

    if (!mSubServer.empty ())
    {
        Json::Value jvObj (Json::objectValue);

        jvObj ["type"]          = "serverStatus";
        jvObj ["server_status"] = strOperatingMode ();
        jvObj ["load_base"]     = (mLastLoadBase = getApp().getFeeTrack ().getLoadBase ());
        jvObj ["load_factor"]   = (mLastLoadFactor = getApp().getFeeTrack ().getLoadFactor ());

        NetworkOPs::SubMapType::const_iterator it = mSubServer.begin ();

        while (it != mSubServer.end ())
        {
            InfoSub::pointer p = it->second.lock ();

            // VFALCO TODO research the possibility of using thread queues and linearizing
            //             the deletion of subscribers with the sending of JSON data.
            if (p)
            {
                p->send (jvObj, true);

                ++it;
            }
            else
            {
                it = mSubServer.erase (it);
            }
        }
    }
}

void NetworkOPs::setMode (OperatingMode om)
{

    if (om == omCONNECTED)
    {
        if (getApp().getLedgerMaster ().getValidatedLedgerAge () < 60)
            om = omSYNCING;
    }

    if (om == omSYNCING)
    {
        if (getApp().getLedgerMaster ().getValidatedLedgerAge () >= 60)
            om = omCONNECTED;
    }

    if ((om > omTRACKING) && mFeatureBlocked)
        om = omTRACKING;

    if (mMode == om)
        return;

    if ((om >= omCONNECTED) && (mMode == omDISCONNECTED))
        mConnectTime = boost::posix_time::second_clock::universal_time ();

    mMode = om;

    Log ((om < mMode) ? lsWARNING : lsINFO) << "STATE->" << strOperatingMode ();
    pubServer ();
}


std::string
NetworkOPs::transactionsSQL (std::string selection, const RippleAddress& account,
                             int32 minLedger, int32 maxLedger, bool descending, uint32 offset, int limit,
                             bool binary, bool count, bool bAdmin)
{
    uint32 NONBINARY_PAGE_LENGTH = 200;
    uint32 BINARY_PAGE_LENGTH = 500;

    uint32 numberOfResults;

    if (count)
        numberOfResults = 1000000000;
    else if (limit < 0)
        numberOfResults = binary ? BINARY_PAGE_LENGTH : NONBINARY_PAGE_LENGTH;
    else if (!bAdmin)
        numberOfResults = std::min (binary ? BINARY_PAGE_LENGTH : NONBINARY_PAGE_LENGTH, static_cast<uint32> (limit));
    else
        numberOfResults = limit;

    std::string maxClause = "";
    std::string minClause = "";

    if (maxLedger != -1)
        maxClause = boost::str (boost::format ("AND AccountTransactions.LedgerSeq <= '%u'") % maxLedger);

    if (minLedger != -1)
        minClause = boost::str (boost::format ("AND AccountTransactions.LedgerSeq >= '%u'") % minLedger);

    std::string sql =
        boost::str (boost::format ("SELECT %s FROM "
                                   "AccountTransactions INNER JOIN Transactions ON Transactions.TransID = AccountTransactions.TransID "
                                   "WHERE Account = '%s' %s %s "
                                   "ORDER BY AccountTransactions.LedgerSeq %s, AccountTransactions.TxnSeq %s, AccountTransactions.TransID %s "
                                   "LIMIT %u, %u;")
                    % selection
                    % account.humanAccountID ()
                    % maxClause
                    % minClause
                    % (descending ? "DESC" : "ASC")
                    % (descending ? "DESC" : "ASC")
                    % (descending ? "DESC" : "ASC")
                    % boost::lexical_cast<std::string> (offset)
                    % boost::lexical_cast<std::string> (numberOfResults)
                   );
    WriteLog (lsTRACE, NetworkOPs) << "txSQL query: " << sql;
    return sql;
}

std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> >
NetworkOPs::getAccountTxs (const RippleAddress& account, int32 minLedger, int32 maxLedger, bool descending, uint32 offset, int limit, bool bAdmin)
{
    // can be called with no locks
    std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> > ret;

    std::string sql = NetworkOPs::transactionsSQL ("AccountTransactions.LedgerSeq,Status,RawTxn,TxnMeta", account,
                      minLedger, maxLedger, descending, offset, limit, false, false, bAdmin);

    {
        Database* db = getApp().getTxnDB ()->getDB ();
        ScopedLock sl (getApp().getTxnDB ()->getDBLock ());

        SQL_FOREACH (db, sql)
        {
            Transaction::pointer txn = Transaction::transactionFromSQL (db, false);

            Serializer rawMeta;
            int metaSize = 2048;
            rawMeta.resize (metaSize);
            metaSize = db->getBinary ("TxnMeta", &*rawMeta.begin (), rawMeta.getLength ());

            if (metaSize > rawMeta.getLength ())
            {
                rawMeta.resize (metaSize);
                db->getBinary ("TxnMeta", &*rawMeta.begin (), rawMeta.getLength ());
            }
            else rawMeta.resize (metaSize);

            if (rawMeta.getLength() == 0)
            { // Work around a bug that could leave the metadata missing
                uint32 seq = static_cast<uint32>(db->getBigInt("AccountTransactions.LedgerSeq"));
                WriteLog(lsWARNING, NetworkOPs) << "Recovering ledger " << seq << ", txn " << txn->getID();
                Ledger::pointer ledger = getLedgerBySeq(seq);
                if (ledger)
                    ledger->pendSave(false);
            }

            if (rawMeta.getLength() == 0)
            { // Work around a bug that could leave the metadata missing
                uint32 seq = static_cast<uint32>(db->getBigInt("AccountTransactions.LedgerSeq"));
                WriteLog(lsWARNING, NetworkOPs) << "Recovering ledger " << seq << ", txn " << txn->getID();
                Ledger::pointer ledger = getLedgerBySeq(seq);
                if (ledger)
                    ledger->pendSave(false);
            }

            TransactionMetaSet::pointer meta = boost::make_shared<TransactionMetaSet> (txn->getID (), txn->getLedger (), rawMeta.getData ());
            ret.push_back (std::pair<Transaction::pointer, TransactionMetaSet::pointer> (txn, meta));
        }
    }

    return ret;
}

std::vector<NetworkOPs::txnMetaLedgerType> NetworkOPs::getAccountTxsB (
    const RippleAddress& account, int32 minLedger, int32 maxLedger, bool descending, uint32 offset, int limit, bool bAdmin)
{
    // can be called with no locks
    std::vector< txnMetaLedgerType> ret;

    std::string sql = NetworkOPs::transactionsSQL ("AccountTransactions.LedgerSeq,Status,RawTxn,TxnMeta", account,
                      minLedger, maxLedger, descending, offset, limit, true/*binary*/, false, bAdmin);

    {
        Database* db = getApp().getTxnDB ()->getDB ();
        ScopedLock sl (getApp().getTxnDB ()->getDBLock ());

        SQL_FOREACH (db, sql)
        {
            int txnSize = 2048;
            Blob rawTxn (txnSize);
            txnSize = db->getBinary ("RawTxn", &rawTxn[0], rawTxn.size ());

            if (txnSize > rawTxn.size ())
            {
                rawTxn.resize (txnSize);
                db->getBinary ("RawTxn", &*rawTxn.begin (), rawTxn.size ());
            }
            else
                rawTxn.resize (txnSize);

            int metaSize = 2048;
            Blob rawMeta (metaSize);
            metaSize = db->getBinary ("TxnMeta", &rawMeta[0], rawMeta.size ());

            if (metaSize > rawMeta.size ())
            {
                rawMeta.resize (metaSize);
                db->getBinary ("TxnMeta", &*rawMeta.begin (), rawMeta.size ());
            }
            else
                rawMeta.resize (metaSize);

            ret.push_back (boost::make_tuple (strHex (rawTxn), strHex (rawMeta), db->getInt ("LedgerSeq")));
        }
    }

    return ret;
}




uint32
NetworkOPs::countAccountTxs (const RippleAddress& account, int32 minLedger, int32 maxLedger)
{
    // can be called with no locks
    uint32 ret = 0;
    std::string sql = NetworkOPs::transactionsSQL ("COUNT(*) AS 'TransactionCount'", account,
                      minLedger, maxLedger, false, 0, -1, true, true, true);

    Database* db = getApp().getTxnDB ()->getDB ();
    ScopedLock sl (getApp().getTxnDB ()->getDBLock ());
    SQL_FOREACH (db, sql)
    {
        ret = db->getInt ("TransactionCount");
    }

    return ret;
}


std::vector<RippleAddress>
NetworkOPs::getLedgerAffectedAccounts (uint32 ledgerSeq)
{
    std::vector<RippleAddress> accounts;
    std::string sql = str (boost::format
                           ("SELECT DISTINCT Account FROM AccountTransactions INDEXED BY AcctLgrIndex WHERE LedgerSeq = '%u';")
                           % ledgerSeq);
    RippleAddress acct;
    {
        Database* db = getApp().getTxnDB ()->getDB ();
        ScopedLock sl (getApp().getTxnDB ()->getDBLock ());
        SQL_FOREACH (db, sql)
        {
            if (acct.setAccountID (db->getStrBinary ("Account")))
                accounts.push_back (acct);
        }
    }
    return accounts;
}

bool NetworkOPs::recvValidation (SerializedValidation::ref val, const std::string& source)
{
    WriteLog (lsDEBUG, NetworkOPs) << "recvValidation " << val->getLedgerHash () << " from " << source;
    return getApp().getValidations ().addValidation (val, source);
}

Json::Value NetworkOPs::getConsensusInfo ()
{
    if (mConsensus)
        return mConsensus->getJson (true);

    Json::Value info = Json::objectValue;
    info["consensus"] = "none";
    return info;
}

Json::Value NetworkOPs::getServerInfo (bool human, bool admin)
{
    Json::Value info = Json::objectValue;

    info ["build_version"] = BuildVersion::getBuildVersion ();
    info ["client_version"] = BuildVersion::getClientVersion ();

    if (theConfig.TESTNET)
        info["testnet"]     = theConfig.TESTNET;

    info["server_state"] = strOperatingMode ();

    if (mNeedNetworkLedger)
        info["network_ledger"] = "waiting";

    info["validation_quorum"] = mLedgerMaster->getMinValidations ();

    if (admin)
    {
        if (theConfig.VALIDATION_PUB.isValid ())
            info["pubkey_validator"] = theConfig.VALIDATION_PUB.humanNodePublic ();
        else
            info["pubkey_validator"] = "none";
    }

    info["pubkey_node"] = getApp().getLocalCredentials ().getNodePublic ().humanNodePublic ();


    info["complete_ledgers"] = getApp().getLedgerMaster ().getCompleteLedgers ();

    if (mFeatureBlocked)
        info["feature_blocked"] = true;

    size_t fp = mFetchPack.getCacheSize ();

    if (fp != 0)
        info["fetch_pack"] = Json::UInt (fp);

    info["peers"] = getApp().getPeers ().getPeerCount ();

    Json::Value lastClose = Json::objectValue;
    lastClose["proposers"] = getApp().getOPs ().getPreviousProposers ();

    if (human)
        lastClose["converge_time_s"] = static_cast<double> (getApp().getOPs ().getPreviousConvergeTime ()) / 1000.0;
    else
        lastClose["converge_time"] = Json::Int (getApp().getOPs ().getPreviousConvergeTime ());

    info["last_close"] = lastClose;

    //  if (mConsensus)
    //      info["consensus"] = mConsensus->getJson();

    if (admin)
        info["load"] = getApp().getJobQueue ().getJson ();

    if (!human)
    {
        info["load_base"] = getApp().getFeeTrack ().getLoadBase ();
        info["load_factor"] = getApp().getFeeTrack ().getLoadFactor ();
    }
    else
    {
        info["load_factor"] =
            static_cast<double> (getApp().getFeeTrack ().getLoadFactor ()) / getApp().getFeeTrack ().getLoadBase ();
        if (admin)
        {
            uint32 base = getApp().getFeeTrack().getLoadBase();
            uint32 fee = getApp().getFeeTrack().getLocalFee();
            if (fee != base)
                info["load_factor_local"] =
                    static_cast<double> (fee) / base;
            fee = getApp().getFeeTrack ().getRemoteFee();
            if (fee != base)
                info["load_factor_net"] =
                    static_cast<double> (fee) / base;
            fee = getApp().getFeeTrack().getClusterFee();
            if (fee != base)
                info["load_factor_cluster"] =
                    static_cast<double> (fee) / base;
        }
    }

    bool valid = false;
    Ledger::pointer lpClosed    = getValidatedLedger ();

    if (lpClosed)
        valid = true;
    else
        lpClosed                = getClosedLedger ();

    if (lpClosed)
    {
        uint64 baseFee = lpClosed->getBaseFee ();
        uint64 baseRef = lpClosed->getReferenceFeeUnits ();
        Json::Value l (Json::objectValue);
        l["seq"]                = Json::UInt (lpClosed->getLedgerSeq ());
        l["hash"]               = lpClosed->getHash ().GetHex ();

        if (!human)
        {
            l["base_fee"]       = Json::Value::UInt (baseFee);
            l["reserve_base"]   = Json::Value::UInt (lpClosed->getReserve (0));
            l["reserve_inc"]    = Json::Value::UInt (lpClosed->getReserveInc ());
            l["close_time"]     = Json::Value::UInt (lpClosed->getCloseTimeNC ());
        }
        else
        {
            l["base_fee_xrp"]       = static_cast<double> (baseFee) / SYSTEM_CURRENCY_PARTS;
            l["reserve_base_xrp"]   =
                static_cast<double> (Json::UInt (lpClosed->getReserve (0) * baseFee / baseRef)) / SYSTEM_CURRENCY_PARTS;
            l["reserve_inc_xrp"]    =
                static_cast<double> (Json::UInt (lpClosed->getReserveInc () * baseFee / baseRef)) / SYSTEM_CURRENCY_PARTS;

            uint32 closeTime = getCloseTimeNC ();
            uint32 lCloseTime = lpClosed->getCloseTimeNC ();

            if (lCloseTime <= closeTime)
            {
                uint32 age = closeTime - lCloseTime;

                if (age < 1000000)
                    l["age"]            = Json::UInt (age);
            }
        }

        if (valid)
            info["validated_ledger"] = l;
        else
            info["closed_ledger"] = l;

        Ledger::pointer lpPublished = getPublishedLedger ();
        if (!lpPublished)
            info["published_ledger"] = "none";
        else if (lpPublished->getLedgerSeq() != lpClosed->getLedgerSeq())
            info["published_ledger"] = lpPublished->getLedgerSeq();
    }

    return info;
}

//
// Monitoring: publisher side
//

Json::Value NetworkOPs::pubBootstrapAccountInfo (Ledger::ref lpAccepted, const RippleAddress& naAccountID)
{
    Json::Value         jvObj (Json::objectValue);

    jvObj["type"]           = "accountInfoBootstrap";
    jvObj["account"]        = naAccountID.humanAccountID ();
    jvObj["owner"]          = getOwnerInfo (lpAccepted, naAccountID);
    jvObj["ledger_index"]   = lpAccepted->getLedgerSeq ();
    jvObj["ledger_hash"]    = lpAccepted->getHash ().ToString ();
    jvObj["ledger_time"]    = Json::Value::UInt (utFromSeconds (lpAccepted->getCloseTimeNC ()));

    return jvObj;
}

void NetworkOPs::pubProposedTransaction (Ledger::ref lpCurrent, SerializedTransaction::ref stTxn, TER terResult)
{
    Json::Value jvObj   = transJson (*stTxn, terResult, false, lpCurrent);

    {
        boost::recursive_mutex::scoped_lock sl (mMonitorLock);
        NetworkOPs::SubMapType::const_iterator it = mSubRTTransactions.begin ();

        while (it != mSubRTTransactions.end ())
        {
            InfoSub::pointer p = it->second.lock ();

            if (p)
            {
                p->send (jvObj, true);
                ++it;
            }
            else
                it = mSubRTTransactions.erase (it);
        }
    }
    AcceptedLedgerTx alt (stTxn, terResult);
    WriteLog (lsTRACE, NetworkOPs) << "pubProposed: " << alt.getJson ();
    pubAccountTransaction (lpCurrent, AcceptedLedgerTx (stTxn, terResult), false);
}

void NetworkOPs::pubLedger (Ledger::ref accepted)
{
    // Ledgers are published only when they acquire sufficient validations
    // Holes are filled across connection loss or other catastrophe

    AcceptedLedger::pointer alpAccepted = AcceptedLedger::makeAcceptedLedger (accepted);
    Ledger::ref lpAccepted = alpAccepted->getLedger ();

    {
        boost::recursive_mutex::scoped_lock sl (mMonitorLock);

        if (!mSubLedger.empty ())
        {
            Json::Value jvObj (Json::objectValue);

            jvObj["type"]           = "ledgerClosed";
            jvObj["ledger_index"]   = lpAccepted->getLedgerSeq ();
            jvObj["ledger_hash"]    = lpAccepted->getHash ().ToString ();
            jvObj["ledger_time"]    = Json::Value::UInt (lpAccepted->getCloseTimeNC ());

            jvObj["fee_ref"]        = Json::UInt (lpAccepted->getReferenceFeeUnits ());
            jvObj["fee_base"]       = Json::UInt (lpAccepted->getBaseFee ());
            jvObj["reserve_base"]   = Json::UInt (lpAccepted->getReserve (0));
            jvObj["reserve_inc"]    = Json::UInt (lpAccepted->getReserveInc ());

            jvObj["txn_count"]      = Json::UInt (alpAccepted->getTxnCount ());

            if (mMode >= omSYNCING)
                jvObj["validated_ledgers"]  = getApp().getLedgerMaster ().getCompleteLedgers ();

            NetworkOPs::SubMapType::const_iterator it = mSubLedger.begin ();

            while (it != mSubLedger.end ())
            {
                InfoSub::pointer p = it->second.lock ();

                if (p)
                {
                    p->send (jvObj, true);
                    ++it;
                }
                else
                    it = mSubLedger.erase (it);
            }
        }
    }

    // Don't lock since pubAcceptedTransaction is locking.
    if (!mSubTransactions.empty () || !mSubRTTransactions.empty () || !mSubAccount.empty () || !mSubRTAccount.empty ())
    {
        BOOST_FOREACH (const AcceptedLedger::value_type & vt, alpAccepted->getMap ())
        {
            WriteLog (lsTRACE, NetworkOPs) << "pubAccepted: " << vt.second->getJson ();
            pubValidatedTransaction (lpAccepted, *vt.second);
        }
    }
}

void NetworkOPs::reportFeeChange ()
{
    if ((getApp().getFeeTrack ().getLoadBase () == mLastLoadBase) &&
            (getApp().getFeeTrack ().getLoadFactor () == mLastLoadFactor))
        return;

    getApp().getJobQueue ().addJob (jtCLIENT, "reportFeeChange->pubServer", BIND_TYPE (&NetworkOPs::pubServer, this));
}

Json::Value NetworkOPs::transJson (const SerializedTransaction& stTxn, TER terResult, bool bValidated,
                                   Ledger::ref lpCurrent)
{
    // This routine should only be used to publish accepted or validated transactions
    Json::Value jvObj (Json::objectValue);
    std::string sToken;
    std::string sHuman;

    transResultInfo (terResult, sToken, sHuman);

    jvObj["type"]           = "transaction";
    jvObj["transaction"]    = stTxn.getJson (0);

    if (bValidated)
    {
        jvObj["ledger_index"]           = lpCurrent->getLedgerSeq ();
        jvObj["ledger_hash"]            = lpCurrent->getHash ().ToString ();
        jvObj["transaction"]["date"]    = lpCurrent->getCloseTimeNC ();
        jvObj["validated"]              = true;

        // WRITEME: Put the account next seq here

    }
    else
    {
        jvObj["validated"]              = false;
        jvObj["ledger_current_index"]   = lpCurrent->getLedgerSeq ();
    }

    jvObj["status"]                 = bValidated ? "closed" : "proposed";
    jvObj["engine_result"]          = sToken;
    jvObj["engine_result_code"]     = terResult;
    jvObj["engine_result_message"]  = sHuman;

    return jvObj;
}

void NetworkOPs::pubValidatedTransaction (Ledger::ref alAccepted, const AcceptedLedgerTx& alTx)
{
    Json::Value jvObj   = transJson (*alTx.getTxn (), alTx.getResult (), true, alAccepted);
    jvObj["meta"] = alTx.getMeta ()->getJson (0);

    {
        boost::recursive_mutex::scoped_lock sl (mMonitorLock);

        NetworkOPs::SubMapType::const_iterator it = mSubTransactions.begin ();

        while (it != mSubTransactions.end ())
        {
            InfoSub::pointer p = it->second.lock ();

            if (p)
            {
                p->send (jvObj, true);
                ++it;
            }
            else
                it = mSubTransactions.erase (it);
        }

        it = mSubRTTransactions.begin ();

        while (it != mSubRTTransactions.end ())
        {
            InfoSub::pointer p = it->second.lock ();

            if (p)
            {
                p->send (jvObj, true);
                ++it;
            }
            else
                it = mSubRTTransactions.erase (it);
        }
    }
    getApp().getOrderBookDB ().processTxn (alAccepted, alTx, jvObj);
    pubAccountTransaction (alAccepted, alTx, true);
}

void NetworkOPs::pubAccountTransaction (Ledger::ref lpCurrent, const AcceptedLedgerTx& alTx, bool bAccepted)
{
    boost::unordered_set<InfoSub::pointer>  notify;
    int                             iProposed   = 0;
    int                             iAccepted   = 0;

    {
        boost::recursive_mutex::scoped_lock sl (mMonitorLock);

        if (!bAccepted && mSubRTAccount.empty ()) return;

        if (!mSubAccount.empty () || (!mSubRTAccount.empty ()) )
        {
            BOOST_FOREACH (const RippleAddress & affectedAccount, alTx.getAffected ())
            {
                SubInfoMapIterator  simiIt  = mSubRTAccount.find (affectedAccount.getAccountID ());

                if (simiIt != mSubRTAccount.end ())
                {
                    NetworkOPs::SubMapType::const_iterator it = simiIt->second.begin ();

                    while (it != simiIt->second.end ())
                    {
                        InfoSub::pointer p = it->second.lock ();

                        if (p)
                        {
                            notify.insert (p);
                            ++it;
                            ++iProposed;
                        }
                        else
                            it = simiIt->second.erase (it);
                    }
                }

                if (bAccepted)
                {
                    simiIt  = mSubAccount.find (affectedAccount.getAccountID ());

                    if (simiIt != mSubAccount.end ())
                    {
                        NetworkOPs::SubMapType::const_iterator it = simiIt->second.begin ();

                        while (it != simiIt->second.end ())
                        {
                            InfoSub::pointer p = it->second.lock ();

                            if (p)
                            {
                                notify.insert (p);
                                ++it;
                                ++iAccepted;
                            }
                            else
                                it = simiIt->second.erase (it);
                        }
                    }
                }
            }
        }
    }
    WriteLog (lsINFO, NetworkOPs) << boost::str (boost::format ("pubAccountTransaction: iProposed=%d iAccepted=%d") % iProposed % iAccepted);

    if (!notify.empty ())
    {
        Json::Value jvObj   = transJson (*alTx.getTxn (), alTx.getResult (), bAccepted, lpCurrent);

        if (alTx.isApplied ())
            jvObj["meta"] = alTx.getMeta ()->getJson (0);

        BOOST_FOREACH (InfoSub::ref isrListener, notify)
        {
            isrListener->send (jvObj, true);
        }
    }
}

//
// Monitoring
//

void NetworkOPs::subAccount (InfoSub::ref isrListener, const boost::unordered_set<RippleAddress>& vnaAccountIDs, uint32 uLedgerIndex, bool rt)
{
    SubInfoMapType& subMap = rt ? mSubRTAccount : mSubAccount;

    // For the connection, monitor each account.
    BOOST_FOREACH (const RippleAddress & naAccountID, vnaAccountIDs)
    {
        WriteLog (lsTRACE, NetworkOPs) << boost::str (boost::format ("subAccount: account: %d") % naAccountID.humanAccountID ());

        isrListener->insertSubAccountInfo (naAccountID, uLedgerIndex);
    }

    boost::recursive_mutex::scoped_lock sl (mMonitorLock);

    BOOST_FOREACH (const RippleAddress & naAccountID, vnaAccountIDs)
    {
        SubInfoMapType::iterator    simIterator = subMap.find (naAccountID.getAccountID ());

        if (simIterator == subMap.end ())
        {
            // Not found, note that account has a new single listner.
            SubMapType  usisElement;
            usisElement[isrListener->getSeq ()] = isrListener;
            subMap.insert (simIterator, make_pair (naAccountID.getAccountID (), usisElement));
        }
        else
        {
            // Found, note that the account has another listener.
            simIterator->second[isrListener->getSeq ()] = isrListener;
        }
    }
}

void NetworkOPs::unsubAccount (uint64 uSeq, const boost::unordered_set<RippleAddress>& vnaAccountIDs, bool rt)
{
    SubInfoMapType& subMap = rt ? mSubRTAccount : mSubAccount;

    // For the connection, unmonitor each account.
    // FIXME: Don't we need to unsub?
    // BOOST_FOREACH(const RippleAddress& naAccountID, vnaAccountIDs)
    // {
    //  isrListener->deleteSubAccountInfo(naAccountID);
    // }

    boost::recursive_mutex::scoped_lock sl (mMonitorLock);

    BOOST_FOREACH (const RippleAddress & naAccountID, vnaAccountIDs)
    {
        SubInfoMapType::iterator    simIterator = subMap.find (naAccountID.getAccountID ());


        if (simIterator == subMap.end ())
        {
            // Not found.  Done.
            nothing ();
        }
        else
        {
            // Found
            simIterator->second.erase (uSeq);

            if (simIterator->second.empty ())
            {
                // Don't need hash entry.
                subMap.erase (simIterator);
            }
        }
    }
}

bool NetworkOPs::subBook (InfoSub::ref isrListener, const uint160& currencyPays, const uint160& currencyGets,
                          const uint160& issuerPays, const uint160& issuerGets)
{
    BookListeners::pointer listeners =
        getApp().getOrderBookDB ().makeBookListeners (currencyPays, currencyGets, issuerPays, issuerGets);

    if (listeners)
        listeners->addSubscriber (isrListener);

    return true;
}

bool NetworkOPs::unsubBook (uint64 uSeq,
                            const uint160& currencyPays, const uint160& currencyGets, const uint160& issuerPays, const uint160& issuerGets)
{
    BookListeners::pointer listeners =
        getApp().getOrderBookDB ().getBookListeners (currencyPays, currencyGets, issuerPays, issuerGets);

    if (listeners)
        listeners->removeSubscriber (uSeq);

    return true;
}

void NetworkOPs::newLCL (int proposers, int convergeTime, uint256 const& ledgerHash)
{
    assert (convergeTime);
    mLastCloseProposers = proposers;
    mLastCloseConvergeTime = convergeTime;
    mLastCloseHash = ledgerHash;
}

uint32 NetworkOPs::acceptLedger ()
{
    // accept the current transaction tree, return the new ledger's sequence
    beginConsensus (mLedgerMaster->getClosedLedger ()->getHash (), mLedgerMaster->getCurrentLedger ());
    mConsensus->simulate ();
    return mLedgerMaster->getCurrentLedger ()->getLedgerSeq ();
}

void NetworkOPs::storeProposal (LedgerProposal::ref proposal, const RippleAddress& peerPublic)
{
    std::list<LedgerProposal::pointer>& props = mStoredProposals[peerPublic.getNodeID ()];

    if (props.size () >= (unsigned) (mLastCloseProposers + 10))
        props.pop_front ();

    props.push_back (proposal);
}

#if 0
void NetworkOPs::subAccountChanges (InfoSub* isrListener, const uint256 uLedgerHash)
{
}

void NetworkOPs::unsubAccountChanges (InfoSub* isrListener)
{
}
#endif

// <-- bool: true=added, false=already there
bool NetworkOPs::subLedger (InfoSub::ref isrListener, Json::Value& jvResult)
{
    Ledger::pointer lpClosed    = getValidatedLedger ();

    if (lpClosed)
    {
        jvResult["ledger_index"]    = lpClosed->getLedgerSeq ();
        jvResult["ledger_hash"]     = lpClosed->getHash ().ToString ();
        jvResult["ledger_time"]     = Json::Value::UInt (lpClosed->getCloseTimeNC ());

        jvResult["fee_ref"]         = Json::UInt (lpClosed->getReferenceFeeUnits ());
        jvResult["fee_base"]        = Json::UInt (lpClosed->getBaseFee ());
        jvResult["reserve_base"]    = Json::UInt (lpClosed->getReserve (0));
        jvResult["reserve_inc"]     = Json::UInt (lpClosed->getReserveInc ());
    }

    if ((mMode >= omSYNCING) && !isNeedNetworkLedger ())
        jvResult["validated_ledgers"]   = getApp().getLedgerMaster ().getCompleteLedgers ();

    boost::recursive_mutex::scoped_lock sl (mMonitorLock);
    return mSubLedger.emplace (isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPs::unsubLedger (uint64 uSeq)
{
    boost::recursive_mutex::scoped_lock sl (mMonitorLock);
    return !!mSubLedger.erase (uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPs::subServer (InfoSub::ref isrListener, Json::Value& jvResult)
{
    uint256         uRandom;

    if (theConfig.RUN_STANDALONE)
        jvResult["stand_alone"] = theConfig.RUN_STANDALONE;

    if (theConfig.TESTNET)
        jvResult["testnet"]     = theConfig.TESTNET;

    RandomNumbers::getInstance ().fillBytes (uRandom.begin (), uRandom.size ());
    jvResult["random"]          = uRandom.ToString ();
    jvResult["server_status"]   = strOperatingMode ();
    jvResult["load_base"]       = getApp().getFeeTrack ().getLoadBase ();
    jvResult["load_factor"]     = getApp().getFeeTrack ().getLoadFactor ();

    boost::recursive_mutex::scoped_lock sl (mMonitorLock);
    return mSubServer.emplace (isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPs::unsubServer (uint64 uSeq)
{
    boost::recursive_mutex::scoped_lock sl (mMonitorLock);
    return !!mSubServer.erase (uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPs::subTransactions (InfoSub::ref isrListener)
{
    boost::recursive_mutex::scoped_lock sl (mMonitorLock);
    return mSubTransactions.emplace (isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPs::unsubTransactions (uint64 uSeq)
{
    boost::recursive_mutex::scoped_lock sl (mMonitorLock);
    return !!mSubTransactions.erase (uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPs::subRTTransactions (InfoSub::ref isrListener)
{
    boost::recursive_mutex::scoped_lock sl (mMonitorLock);
    return mSubTransactions.emplace (isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPs::unsubRTTransactions (uint64 uSeq)
{
    boost::recursive_mutex::scoped_lock sl (mMonitorLock);
    return !!mSubTransactions.erase (uSeq);
}

InfoSub::pointer NetworkOPs::findRpcSub (const std::string& strUrl)
{
    boost::recursive_mutex::scoped_lock sl (mMonitorLock);

    subRpcMapType::iterator it = mRpcSubMap.find (strUrl);

    if (it != mRpcSubMap.end ())
        return it->second;

    return InfoSub::pointer ();
}

InfoSub::pointer NetworkOPs::addRpcSub (const std::string& strUrl, InfoSub::ref rspEntry)
{
    boost::recursive_mutex::scoped_lock sl (mMonitorLock);

    mRpcSubMap.emplace (strUrl, rspEntry);

    return rspEntry;
}

// FIXME : support iLimit.
void NetworkOPs::getBookPage (Ledger::pointer lpLedger, const uint160& uTakerPaysCurrencyID, const uint160& uTakerPaysIssuerID, const uint160& uTakerGetsCurrencyID, const uint160& uTakerGetsIssuerID, const uint160& uTakerID, const bool bProof, const unsigned int iLimit, const Json::Value& jvMarker, Json::Value& jvResult)
{
    Json::Value&    jvOffers    = (jvResult["offers"] = Json::Value (Json::arrayValue));

    std::map<uint160, STAmount> umBalance;
    const uint256   uBookBase   = Ledger::getBookBase (uTakerPaysCurrencyID, uTakerPaysIssuerID, uTakerGetsCurrencyID, uTakerGetsIssuerID);
    const uint256   uBookEnd    = Ledger::getQualityNext (uBookBase);
    uint256         uTipIndex   = uBookBase;

    WriteLog (lsTRACE, NetworkOPs) << boost::str (boost::format ("getBookPage: uTakerPaysCurrencyID=%s uTakerPaysIssuerID=%s") % STAmount::createHumanCurrency (uTakerPaysCurrencyID) % RippleAddress::createHumanAccountID (uTakerPaysIssuerID));
    WriteLog (lsTRACE, NetworkOPs) << boost::str (boost::format ("getBookPage: uTakerGetsCurrencyID=%s uTakerGetsIssuerID=%s") % STAmount::createHumanCurrency (uTakerGetsCurrencyID) % RippleAddress::createHumanAccountID (uTakerGetsIssuerID));
    WriteLog (lsTRACE, NetworkOPs) << boost::str (boost::format ("getBookPage: uBookBase=%s") % uBookBase);
    WriteLog (lsTRACE, NetworkOPs) << boost::str (boost::format ("getBookPage:  uBookEnd=%s") % uBookEnd);
    WriteLog (lsTRACE, NetworkOPs) << boost::str (boost::format ("getBookPage: uTipIndex=%s") % uTipIndex);

    LedgerEntrySet  lesActive (lpLedger, tapNONE, true);

    bool            bDone           = false;
    bool            bDirectAdvance  = true;

    SLE::pointer    sleOfferDir;
    uint256         uOfferIndex;
    unsigned int    uBookEntry;
    STAmount        saDirRate;

    unsigned int    iLeft           = iLimit;

    if ((iLeft == 0) || (iLeft > 300))
        iLeft = 300;

    uint32  uTransferRate   = lesActive.rippleTransferRate (uTakerGetsIssuerID);

    while (!bDone && (iLeft > 0))
    {
        if (bDirectAdvance)
        {
            bDirectAdvance  = false;

            WriteLog (lsTRACE, NetworkOPs) << "getBookPage: bDirectAdvance";

            sleOfferDir     = lesActive.entryCache (ltDIR_NODE, lpLedger->getNextLedgerIndex (uTipIndex, uBookEnd));

            if (!sleOfferDir)
            {
                WriteLog (lsTRACE, NetworkOPs) << "getBookPage: bDone";
                bDone           = true;
            }
            else
            {
                uTipIndex       = sleOfferDir->getIndex ();
                saDirRate       = STAmount::setRate (Ledger::getQuality (uTipIndex));
                SLE::pointer    sleBookNode;

                lesActive.dirFirst (uTipIndex, sleBookNode, uBookEntry, uOfferIndex);

                WriteLog (lsTRACE, NetworkOPs) << boost::str (boost::format ("getBookPage:   uTipIndex=%s") % uTipIndex);
                WriteLog (lsTRACE, NetworkOPs) << boost::str (boost::format ("getBookPage: uOfferIndex=%s") % uOfferIndex);
            }
        }

        if (!bDone)
        {
            SLE::pointer    sleOffer        = lesActive.entryCache (ltOFFER, uOfferIndex);
            const uint160   uOfferOwnerID   = sleOffer->getFieldAccount160 (sfAccount);
            const STAmount& saTakerGets     = sleOffer->getFieldAmount (sfTakerGets);
            const STAmount& saTakerPays     = sleOffer->getFieldAmount (sfTakerPays);
            STAmount        saOwnerFunds;

            if (uTakerGetsIssuerID == uOfferOwnerID)
            {
                // If offer is selling issuer's own IOUs, it is fully funded.
                saOwnerFunds    = saTakerGets;
            }
            else
            {
                std::map<uint160, STAmount>::const_iterator umBalanceEntry  = umBalance.find (uOfferOwnerID);

                if (umBalanceEntry != umBalance.end ())
                {
                    // Found in running balance table.

                    saOwnerFunds    = umBalanceEntry->second;
                    // WriteLog (lsINFO, NetworkOPs) << boost::str(boost::format("getBookPage: saOwnerFunds=%s (cached)") % saOwnerFunds.getFullText());
                }
                else
                {
                    // Did not find balance in table.

                    saOwnerFunds    = lesActive.accountHolds (uOfferOwnerID, uTakerGetsCurrencyID, uTakerGetsIssuerID);

                    // WriteLog (lsINFO, NetworkOPs) << boost::str(boost::format("getBookPage: saOwnerFunds=%s (new)") % saOwnerFunds.getFullText());
                    if (saOwnerFunds.isNegative ())
                    {
                        // Treat negative funds as zero.

                        saOwnerFunds.zero ();
                    }
                }
            }

            Json::Value jvOffer = sleOffer->getJson (0);

            STAmount    saTakerGetsFunded;
            STAmount    saOwnerFundsLimit;
            uint32      uOfferRate;


            if (uTransferRate != QUALITY_ONE                // Have a tranfer fee.
                    && uTakerID != uTakerGetsIssuerID           // Not taking offers of own IOUs.
                    && uTakerGetsIssuerID != uOfferOwnerID)     // Offer owner not issuing ownfunds
            {
                // Need to charge a transfer fee to offer owner.
                uOfferRate          = uTransferRate;
                saOwnerFundsLimit   = STAmount::divide (saOwnerFunds, STAmount (CURRENCY_ONE, ACCOUNT_ONE, uOfferRate, -9));
            }
            else
            {
                uOfferRate          = QUALITY_ONE;
                saOwnerFundsLimit   = saOwnerFunds;
            }

            if (saOwnerFundsLimit >= saTakerGets)
            {
                // Sufficient funds no shenanigans.
                saTakerGetsFunded   = saTakerGets;
            }
            else
            {
                // WriteLog (lsINFO, NetworkOPs) << boost::str(boost::format("getBookPage:  saTakerGets=%s") % saTakerGets.getFullText());
                // WriteLog (lsINFO, NetworkOPs) << boost::str(boost::format("getBookPage:  saTakerPays=%s") % saTakerPays.getFullText());
                // WriteLog (lsINFO, NetworkOPs) << boost::str(boost::format("getBookPage: saOwnerFunds=%s") % saOwnerFunds.getFullText());
                // WriteLog (lsINFO, NetworkOPs) << boost::str(boost::format("getBookPage:    saDirRate=%s") % saDirRate.getText());
                // WriteLog (lsINFO, NetworkOPs) << boost::str(boost::format("getBookPage:     multiply=%s") % STAmount::multiply(saTakerGetsFunded, saDirRate).getFullText());
                // WriteLog (lsINFO, NetworkOPs) << boost::str(boost::format("getBookPage:     multiply=%s") % STAmount::multiply(saTakerGetsFunded, saDirRate, saTakerPays).getFullText());

                // Only provide, if not fully funded.

                saTakerGetsFunded   = saOwnerFundsLimit;

                saTakerGetsFunded.setJson (jvOffer["taker_gets_funded"]);
                std::min (saTakerPays, STAmount::multiply (saTakerGetsFunded, saDirRate, saTakerPays)).setJson (jvOffer["taker_pays_funded"]);
            }

            STAmount    saOwnerPays     = (QUALITY_ONE == uOfferRate)
                                          ? saTakerGetsFunded
                                          : std::min (saOwnerFunds, STAmount::multiply (saTakerGetsFunded, STAmount (CURRENCY_ONE, ACCOUNT_ONE, uOfferRate, -9)));

            umBalance[uOfferOwnerID]    = saOwnerFunds - saOwnerPays;

            if (!saOwnerFunds.isZero () || uOfferOwnerID == uTakerID)
            {
                // Only provide funded offers and offers of the taker.
                Json::Value& jvOf   = jvOffers.append (jvOffer);
                jvOf["quality"]     = saDirRate.getText ();
                --iLeft;
            }

            if (!lesActive.dirNext (uTipIndex, sleOfferDir, uBookEntry, uOfferIndex))
            {
                bDirectAdvance  = true;
            }
            else
            {
                WriteLog (lsTRACE, NetworkOPs) << boost::str (boost::format ("getBookPage: uOfferIndex=%s") % uOfferIndex);
            }
        }
    }

    //  jvResult["marker"]  = Json::Value(Json::arrayValue);
    //  jvResult["nodes"]   = Json::Value(Json::arrayValue);
}

static void fpAppender (protocol::TMGetObjectByHash* reply, uint32 ledgerSeq,
                        const uint256& hash, const Blob& blob)
{
    protocol::TMIndexedObject& newObj = * (reply->add_objects ());
    newObj.set_ledgerseq (ledgerSeq);
    newObj.set_hash (hash.begin (), 256 / 8);
    newObj.set_data (&blob[0], blob.size ());
}

void NetworkOPs::makeFetchPack (Job&, boost::weak_ptr<Peer> wPeer,
                                boost::shared_ptr<protocol::TMGetObjectByHash> request,
                                Ledger::pointer wantLedger, Ledger::pointer haveLedger, uint32 uUptime)
{
    if (UptimeTimer::getInstance ().getElapsedSeconds () > (uUptime + 1))
    {
        WriteLog (lsINFO, NetworkOPs) << "Fetch pack request got stale";
        return;
    }

    if (getApp().getFeeTrack ().isLoadedLocal ())
    {
        WriteLog (lsINFO, NetworkOPs) << "Too busy to make fetch pack";
        return;
    }

    try
    {
        Peer::pointer peer = wPeer.lock ();

        if (!peer)
            return;

        protocol::TMGetObjectByHash reply;
        reply.set_query (false);

        if (request->has_seq ())
            reply.set_seq (request->seq ());

        reply.set_ledgerhash (request->ledgerhash ());
        reply.set_type (protocol::TMGetObjectByHash::otFETCH_PACK);

        do
        {
            uint32 lSeq = wantLedger->getLedgerSeq ();

            protocol::TMIndexedObject& newObj = *reply.add_objects ();
            newObj.set_hash (wantLedger->getHash ().begin (), 256 / 8);
            Serializer s (256);
            s.add32 (HashPrefix::ledgerMaster);
            wantLedger->addRaw (s);
            newObj.set_data (s.getDataPtr (), s.getLength ());
            newObj.set_ledgerseq (lSeq);

            wantLedger->peekAccountStateMap ()->getFetchPack (haveLedger->peekAccountStateMap ().get (), true, 1024,
                    BIND_TYPE (fpAppender, &reply, lSeq, P_1, P_2));

            if (wantLedger->getAccountHash ().isNonZero ())
                wantLedger->peekTransactionMap ()->getFetchPack (NULL, true, 256,
                        BIND_TYPE (fpAppender, &reply, lSeq, P_1, P_2));

            if (reply.objects ().size () >= 256)
                break;

            haveLedger = MOVE_P(wantLedger);
            wantLedger = getLedgerByHash (haveLedger->getParentHash ());
        }
        while (wantLedger && (UptimeTimer::getInstance ().getElapsedSeconds () <= (uUptime + 1)));

        WriteLog (lsINFO, NetworkOPs) << "Built fetch pack with " << reply.objects ().size () << " nodes";
        PackedMessage::pointer msg = boost::make_shared<PackedMessage> (reply, protocol::mtGET_OBJECTS);
        peer->sendPacket (msg, false);
    }
    catch (...)
    {
        WriteLog (lsWARNING, NetworkOPs) << "Exception building fetch pach";
    }
}

void NetworkOPs::sweepFetchPack ()
{
    mFetchPack.sweep ();
}

void NetworkOPs::addFetchPack (uint256 const& hash, boost::shared_ptr< Blob >& data)
{
    mFetchPack.canonicalize (hash, data);
}

bool NetworkOPs::getFetchPack (uint256 const& hash, Blob& data)
{
    bool ret = mFetchPack.retrieve (hash, data);

    if (!ret)
        return false;

    mFetchPack.del (hash, false);

    if (hash != Serializer::getSHA512Half (data))
    {
        WriteLog (lsWARNING, NetworkOPs) << "Bad entry in fetch pack";
        return false;
    }

    return true;
}

bool NetworkOPs::shouldFetchPack (uint32 seq)
{
    uint32 now = getNetworkTimeNC ();

    if ((mLastFetchPack == now) || ((mLastFetchPack + 1) == now))
        return false;

    if (seq < mFetchSeq) // fetch pack has only data for ledgers ahead of where we are
        mFetchPack.clear ();
    else
        mFetchPack.sweep ();

    int size = mFetchPack.getCacheSize ();

    if (size == 0)
    {
        // VFALCO TODO Give this magic number a name
        //
        mFetchSeq = static_cast<uint32> (-1);
    }
    else if (mFetchPack.getCacheSize () > 64)
    {
        return false;
    }

    mLastFetchPack = now;

    return true;
}

int NetworkOPs::getFetchSize ()
{
    return mFetchPack.getCacheSize ();
}

void NetworkOPs::gotFetchPack (bool progress, uint32 seq)
{
    mLastFetchPack = 0;
    mFetchSeq = seq;        // earliest pack we have data on
    getApp().getJobQueue ().addJob (jtLEDGER_DATA, "gotFetchPack",
                                   BIND_TYPE (&InboundLedgers::gotFetchPack, &getApp().getInboundLedgers (), P_1));
}

void NetworkOPs::missingNodeInLedger (uint32 seq)
{
    WriteLog (lsWARNING, NetworkOPs) << "We are missing a node in ledger " << seq;
    uint256 hash = getApp().getLedgerMaster ().getHashBySeq (seq);

    if (hash.isNonZero ())
        getApp().getInboundLedgers ().findCreate (hash, seq);
}

void NetworkOPs::doClusterReport ()
{
    ClusterNodeStatus us("", getApp().getFeeTrack().getLocalFee(), getNetworkTimeNC());
    if (!getApp().getUNL().nodeUpdate(getApp().getLocalCredentials().getNodePublic(), us))
    {
        WriteLog (lsDEBUG, NetworkOPs) << "To soon to send cluster update";
        return;
    }

    std::map<RippleAddress, ClusterNodeStatus> nodes = getApp().getUNL().getClusterStatus();

    protocol::TMCluster cluster;
    for (std::map<RippleAddress, ClusterNodeStatus>::iterator it = nodes.begin(),
            end = nodes.end(); it != end; ++it)
    {
        protocol::TMClusterNode& node = *cluster.add_clusternodes();
        node.set_publickey(it->first.humanNodePublic());
        node.set_reporttime(it->second.getReportTime());
        node.set_nodeload(it->second.getLoadFee());
        if (!it->second.getName().empty())
            node.set_nodename(it->second.getName());
    }

    PackedMessage::pointer message = boost::make_shared<PackedMessage>(cluster, protocol::mtCLUSTER);
    getApp().getPeers().relayMessageCluster (NULL, message);
}

// vim:ts=4
