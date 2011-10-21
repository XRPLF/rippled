#include "ValidationCollection.h"
#include "Application.h"
#include "NewcoinAddress.h"
#include "Config.h"

#include <boost/foreach.hpp>
using namespace std;


bool ValidationCollection::hasValidation(uint256& ledgerHash,uint160& hanko,uint32 seqnum)
{
	if(mValidations.count(ledgerHash))
	{
		BOOST_FOREACH(newcoin::Validation& valid,mValidations[ledgerHash])
		{
			if( valid.seqnum()==seqnum &&
				NewcoinAddress::protobufToInternal(valid.hanko()) == hanko) return(true);
		}
	}

	if(mIgnoredValidations.count(ledgerHash))
	{
		BOOST_FOREACH(newcoin::Validation& valid,mIgnoredValidations[ledgerHash])
		{
			if( valid.seqnum()==seqnum &&
				NewcoinAddress::protobufToInternal(valid.hanko()) == hanko) return(true);
		}
	}
	return(false);
}

// TODO: we are adding our own validation
// TODO: when do we check if we are with the consensus?
// TODO: throw out lower seqnums
void ValidationCollection::addValidation(newcoin::Validation& valid)
{
	// TODO: make sure the validation is valid

	uint256 hash=Transaction::protobufToInternalHash(valid.hash());
	uint160 hanko=NewcoinAddress::protobufToInternal(valid.hanko());
	
	// make sure we don't already have this validation
	if(hasValidation(hash,hanko,valid.seqnum())) return;

	// check if we care about this hanko
	if( theApp->getUNL().findHanko(valid.hanko()) )
	{
		mValidations[hash].push_back(valid);
		
		theApp->getLedgerMaster().checkConsensus(valid.ledgerindex());
	}else
	{
		mIgnoredValidations[hash].push_back(valid);
	}
}


// TODO: optimize. We can at least cache what ledgers are compatible
// a validation can be in multiple groups since compatibility isn't transitive
// Sometimes things are just complex
void ValidationCollection::addToGroup(newcoin::Validation& newValid)
{
	if(mGroupValidations.count(newValid.ledgerindex()))
	{
		bool canReturn=false;
		// see if this hash is already on the list. If so add it there.
		vector< vector<newcoin::Validation> >& groups=mGroupValidations[newValid.ledgerindex()];
		vector<newcoin::Validation>& groupList=vector<newcoin::Validation>();
		BOOST_FOREACH(groupList,groups)
		{
			BOOST_FOREACH(newcoin::Validation& valid,groupList)
			{
				if(valid.hash()==newValid.hash())
				{
					groupList.push_back(newValid);
					canReturn=true;
					break;
				}
			}
		}

		if(canReturn) return;
		// this is a validation of a new ledger hash

		uint256 newHash=Transaction::protobufToInternalHash(newValid.hash());
		Ledger::pointer newLedger=theApp->getLedgerMaster().getLedger(newHash);
		if(newLedger)
		{ // see if this ledger is compatible with any groups
			BOOST_FOREACH(groupList,groups)
			{
				bool compatible=true;
				BOOST_FOREACH(newcoin::Validation& valid,groupList)
				{
					uint256 hash=Transaction::protobufToInternalHash(valid.hash());
					Ledger::pointer ledger=theApp->getLedgerMaster().getLedger(hash);
					if(ledger)
					{
						if(!ledger->isCompatible(newLedger))
						{  // not compatible with this group
							compatible=false;
							break; 
						}
					}else 
					{ // we can't tell if it is compatible
						compatible=false;
						break;
					}
				}

				if(compatible) groupList.push_back(newValid);
			}

		}

		// also add to its own group in case 
		groupList.push_back(newValid);
		groups.push_back(groupList);
		
	}else
	{ // this is the first validation of this ledgerindex
		mGroupValidations[newValid.ledgerindex()][0].push_back(newValid);
	}

	
}

vector<newcoin::Validation>* ValidationCollection::getValidations(uint32 ledgerIndex)
{
	if(mGroupValidations.count(ledgerIndex))
	{
		return(&(mGroupValidations[ledgerIndex]));
	}
	return(NULL);
}


// look through all the validated hashes at that index
// put the ledgers into compatible groups
// Pick the group with the most votes
bool ValidationCollection::getConsensusLedgers(uint32 ledgerIndex, list<uint256>& retHashs)
{
	vector<newcoin::Validation>* valids=getValidations(ledgerIndex);
	if(valids)
	{
		vector< pair<int, list<uint256> > > compatibleGroups;

		map<uint256, int> voteCounts;
		BOOST_FOREACH(newcoin::Validation valid,*valids)
		{
			uint256 hash=Transaction::protobufToInternalHash(valid.hash());
			Ledger::pointer testLedger=theApp->getLedgerMaster().getLedger(hash);
			if(testLedger)
			{

			}

			voteCounts[  ] += 1;
		}
		bool ret=false;
		int maxVotes=theConfig.MIN_VOTES_FOR_CONSENSUS;
		pair<uint256, int>& vote=pair<uint256, int>();
		BOOST_FOREACH(vote,voteCounts)
		{
			if(vote.second>maxVotes)
			{
				maxVotes=vote.second;
				retHash=vote.first;
				ret=true;
			}
		}
		return(ret);
	}
	return(false);
}