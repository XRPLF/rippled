#include "ValidationCollection.h"
#include "Application.h"
#include "NewcoinAddress.h"
#include "Config.h"
#include "Convertion.h"
#include <boost/foreach.hpp>
using namespace std;

/*
We need to group validations into compatible groups.
We can make one super ledger of all the transactions in each compatible group.
Then we just have to check this ledger to see if a new ledger is compatible
This is also the ledger we hand back when we ask for the consensus ledger

*/
ValidationCollection::ValidationCollection()
{

}

void ValidationCollection::save()
{

}
void ValidationCollection::load()
{

}

bool ValidationCollection::hasValidation(uint256& ledgerHash,uint160& hanko,uint32 seqnum)
{
	if(mValidations.count(ledgerHash))
	{
		BOOST_FOREACH(newcoin::Validation& valid,mValidations[ledgerHash])
		{
			if( valid.seqnum()==seqnum &&
				protobufTo160(valid.hanko()) == hanko) return(true);
		}
	}

	if(mIgnoredValidations.count(ledgerHash))
	{
		BOOST_FOREACH(newcoin::Validation& valid,mIgnoredValidations[ledgerHash])
		{
			if( valid.seqnum()==seqnum &&
				protobufTo160(valid.hanko()) == hanko) return(true);
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

	uint256 hash=protobufTo256(valid.hash());
	uint160 hanko=protobufTo160(valid.hanko());
	
	// make sure we don't already have this validation
	if(hasValidation(hash,hanko,valid.seqnum())) return;

	// check if we care about this hanko
	int validity=theApp->getUNL().checkValid(valid);
	if( validity==1 )
	{
		mValidations[hash].push_back(valid);
		mIndexValidations[valid.ledgerindex()].push_back(valid);
		addToGroup(valid);
		
		theApp->getLedgerMaster().checkConsensus(valid.ledgerindex());
	}else if(validity==0)
	{
		mIgnoredValidations[hash].push_back(valid);
	}else
	{ // the signature wasn't valid
		cout << "Invalid Validation" << endl;
	}
}


bool ValidationCollection::Group::addIfCompatible(Ledger::pointer ledger,newcoin::Validation& valid)
{
	if(mSuperLedger)
	{
		if(mSuperLedger->isCompatible(ledger))
		{
			mValidations.push_back(valid);
			mSuperLedger->mergeIn(ledger);
		}
	}
	return(false);
}

// TODO: optimize. We can at least cache what ledgers are compatible
// a validation can be in multiple groups since compatibility isn't transitive
// Sometimes things are just complex
void ValidationCollection::addToGroup(newcoin::Validation& newValid)
{

	if(mIndexGroups.count(newValid.ledgerindex()))
	{
		bool canReturn=false;
		// see if this hash is already on the list. If so add it there.
		vector< Group >& groups=mIndexGroups[newValid.ledgerindex()];
		BOOST_FOREACH(Group& group,groups)
		{
			BOOST_FOREACH(newcoin::Validation& valid,group.mValidations)
			{
				if(valid.hash()==newValid.hash())
				{
					group.mValidations.push_back(newValid);
					canReturn=true;
					break;
				}
			}
		}

		if(canReturn) return;
		// this is a validation of a new ledger hash

		uint256 newHash=protobufTo256(newValid.hash());
		Ledger::pointer newLedger=theApp->getLedgerMaster().getLedger(newHash);
		if(newLedger)
		{ // see if this ledger is compatible with any groups
			bool foundGroup=false;
			BOOST_FOREACH(Group& group,groups)
			{
				if(group.addIfCompatible(newLedger,newValid)) foundGroup=true;
			}

			if(!foundGroup)
			{ // this validation didn't fit in any of the other groups
				// we need to make a new group for it and see what validations fit it
				Group& newGroup=mIndexGroups[newValid.ledgerindex()][groups.size()];

				newGroup.mValidations.push_back(newValid);
				newGroup.mSuperLedger=Ledger::pointer(new Ledger(newLedger));  // since this super ledger gets modified and we don't want to screw the original

				BOOST_FOREACH(newcoin::Validation& valid,mIndexValidations[newValid.ledgerindex()])
				{
					uint256 hash=protobufTo256(valid.hash());
					Ledger::pointer ledger=theApp->getLedgerMaster().getLedger(hash);
					newGroup.addIfCompatible(ledger,valid);
				}
			}

		}else
		{	// we don't have a ledger for this validation
			// add to its own group since we can't check if it is compatible
			int newIndex=groups.size();
			mIndexGroups[newValid.ledgerindex()][newIndex].mValidations.push_back(newValid);
		}
	}else
	{ // this is the first validation of this ledgerindex
		uint256 newHash=protobufTo256(newValid.hash());
		mIndexGroups[newValid.ledgerindex()][0].mValidations.push_back(newValid);
		mIndexGroups[newValid.ledgerindex()][0].mSuperLedger=theApp->getLedgerMaster().getLedger(newHash);
	}
}

vector<newcoin::Validation>* ValidationCollection::getValidations(uint32 ledgerIndex)
{
	if(mIndexValidations.count(ledgerIndex))
	{
		return(&(mIndexValidations[ledgerIndex]));
	}
	return(NULL);
}


// look through all the validated hashes at that index
// put the ledgers into compatible groups
// Pick the group with the most votes
bool ValidationCollection::getConsensusLedger(uint32 ledgerIndex, uint256& ourHash, Ledger::pointer& retLedger, uint256& retHash)
{
	bool ret=false;
	if(mIndexGroups.count(ledgerIndex))
	{
		
		unsigned int maxVotes=theConfig.MIN_VOTES_FOR_CONSENSUS;
		vector<newcoin::Validation>& mostValid=vector<newcoin::Validation>();
		vector< Group >& groups=mIndexGroups[ledgerIndex];
		Group& maxGroup=Group();
		BOOST_FOREACH(Group& group,groups)
		{
			if(group.mValidations.size()>maxVotes)
			{
				maxVotes=group.mValidations.size();
				retLedger=group.mSuperLedger;
				maxGroup=group;
				if(!retLedger) retHash=protobufTo256(group.mValidations[0].hash());
				ret=true;
			}
		}
		if(ret)
		{
			// should also return false if we are in the consensus
			BOOST_FOREACH(newcoin::Validation& valid, maxGroup.mValidations)
			{
				if(protobufTo256(valid.hash()) == ourHash) return(false);
			}
		}
	}
	
	return(ret);
}