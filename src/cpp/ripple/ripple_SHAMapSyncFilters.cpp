
ConsensusTransSetSF::ConsensusTransSetSF ()
{
}

void ConsensusTransSetSF::gotNode (bool fromFilter, const SHAMapNode& id, uint256 const& nodeHash,
                                   Blob const& nodeData, SHAMapTreeNode::TNType type)
{
    if (fromFilter)
        return;

    theApp->getTempNodeCache ().store (nodeHash, nodeData);

    if ((type == SHAMapTreeNode::tnTRANSACTION_NM) && (nodeData.size () > 16))
    {
        // this is a transaction, and we didn't have it
        WriteLog (lsDEBUG, TransactionAcquire) << "Node on our acquiring TX set is TXN we don't have";

        try
        {
            Serializer s (nodeData.begin () + 4, nodeData.end ()); // skip prefix
            SerializerIterator sit (s);
            SerializedTransaction::pointer stx = boost::make_shared<SerializedTransaction> (boost::ref (sit));
            assert (stx->getTransactionID () == nodeHash);
            theApp->getJobQueue ().addJob (jtTRANSACTION, "TXS->TXN",
                                           BIND_TYPE (&NetworkOPs::submitTransaction, &theApp->getOPs (), P_1, stx, NetworkOPs::stCallback ()));
        }
        catch (...)
        {
            WriteLog (lsWARNING, TransactionAcquire) << "Fetched invalid transaction in proposed set";
        }
    }
}

bool ConsensusTransSetSF::haveNode (const SHAMapNode& id, uint256 const& nodeHash,
                                    Blob& nodeData)
{
    if (theApp->getTempNodeCache ().retrieve (nodeHash, nodeData))
        return true;

    Transaction::pointer txn = Transaction::load (nodeHash);

    if (txn)
    {
        // this is a transaction, and we have it
        WriteLog (lsDEBUG, TransactionAcquire) << "Node in our acquiring TX set is TXN we have";
        Serializer s;
        s.add32 (sHP_TransactionID);
        txn->getSTransaction ()->add (s, true);
        assert (s.getSHA512Half () == nodeHash);
        nodeData = s.peekData ();
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------

AccountStateSF::AccountStateSF (uint32 ledgerSeq)
    : mLedgerSeq (ledgerSeq)
{
}

void AccountStateSF::gotNode (bool fromFilter,
                              SHAMapNode const& id,
                              uint256 const& nodeHash,
                              Blob const& nodeData,
                              SHAMapTreeNode::TNType)
{
    theApp->getHashedObjectStore ().store (hotACCOUNT_NODE, mLedgerSeq, nodeData, nodeHash);
}

bool AccountStateSF::haveNode (SHAMapNode const& id,
                               uint256 const& nodeHash,
                               Blob& nodeData)
{
    return theApp->getOPs ().getFetchPack (nodeHash, nodeData);
}

//------------------------------------------------------------------------------

TransactionStateSF::TransactionStateSF (uint32 ledgerSeq)
    : mLedgerSeq (ledgerSeq)
{
}

void TransactionStateSF::gotNode (bool fromFilter,
                                  SHAMapNode const& id,
                                  uint256 const& nodeHash,
                                  Blob const& nodeData,
                                  SHAMapTreeNode::TNType type)
{
    theApp->getHashedObjectStore ().store (
        (type == SHAMapTreeNode::tnTRANSACTION_NM) ? hotTRANSACTION : hotTRANSACTION_NODE,
        mLedgerSeq,
        nodeData,
        nodeHash);
}

bool TransactionStateSF::haveNode (SHAMapNode const& id,
                                   uint256 const& nodeHash,
                                   Blob& nodeData)
{
    return theApp->getOPs ().getFetchPack (nodeHash, nodeData);
}
