
TaggedCache <uint256, AcceptedLedger, UptimeTimerAdapter> AcceptedLedger::s_cache("AcceptedLedger", 4, 60);

AcceptedLedger::AcceptedLedger(Ledger::ref ledger) : mLedger(ledger)
{
	SHAMap& txSet = *ledger->peekTransactionMap();
	for (SHAMapItem::pointer item = txSet.peekFirstItem(); !!item; item = txSet.peekNextItem(item->getTag()))
	{
		SerializerIterator sit(item->peekSerializer());
		insert(boost::make_shared<AcceptedLedgerTx>(ledger->getLedgerSeq(), boost::ref(sit)));
	}
}

AcceptedLedger::pointer AcceptedLedger::makeAcceptedLedger(Ledger::ref ledger)
{
	AcceptedLedger::pointer ret = s_cache.fetch(ledger->getHash());
	if (ret)
		return ret;
	ret = AcceptedLedger::pointer(new AcceptedLedger(ledger));
	s_cache.canonicalize(ledger->getHash(), ret);
	return ret;
}

void AcceptedLedger::insert(AcceptedLedgerTx::ref at)
{
	assert(mMap.find(at->getIndex()) == mMap.end());
	mMap.insert(std::make_pair(at->getIndex(), at));
}

AcceptedLedgerTx::pointer AcceptedLedger::getTxn(int i) const
{
	map_t::const_iterator it = mMap.find(i);
	if (it == mMap.end())
		return AcceptedLedgerTx::pointer();
	return it->second;
}
