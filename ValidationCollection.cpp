#include "ValidationCollection.h"
#include "Application.h"
#include "NewcoinAddress.h"
#include <boost/foreach.hpp>

bool ValidationCollection::hasValidation(uint256& ledgerHash,uint160& hanko)
{
	if(mValidations.count(ledgerHash))
	{
		BOOST_FOREACH(newcoin::Validation& valid,mValidations[ledgerHash])
		{
			if(NewcoinAddress::protobufToInternal(valid.hanko()) == hanko) return(true);
		}
	}
	if(mIgnoredValidations.count(ledgerHash))
	{
		BOOST_FOREACH(newcoin::Validation& valid,mIgnoredValidations[ledgerHash])
		{
			if(NewcoinAddress::protobufToInternal(valid.hanko()) == hanko) return(true);
		}
	}
	return(false);
}

// TODO: when do we check if we are with the consensus?
void ValidationCollection::addValidation(newcoin::Validation& valid)
{
	// TODO: make sure the validation is valid

	uint256 hash=Transaction::protobufToInternalHash(valid.hash());
	uint160 hanko=NewcoinAddress::protobufToInternal(valid.hanko());
	
	// make sure we don't already have this validation
	if(hasValidation(hash,hanko)) return;

	// check if we care about this hanko
	if( theApp->getUNL().findHanko(valid.hanko()) )
	{
		mValidations[hash].push_back(valid);
	}else
	{
		mIgnoredValidations[hash].push_back(valid);
	}

	mMapIndexToValid[valid.ledgerindex()].push_back(valid);
}

std::vector<newcoin::Validation>* ValidationCollection::getValidations(uint32 ledgerIndex)
{
	if(mMapIndexToValid.count(ledgerIndex))
	{
		return(&(mMapIndexToValid[ledgerIndex]));
	}
	return(NULL);
}