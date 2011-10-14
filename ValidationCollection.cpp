#include "ValidationCollection.h"
#include "Application.h"

void ValidationCollection::addValidation(newcoin::Validation& valid)
{
	// TODO:
	// make sure we don't already have this validation
	// check if we care about this hanko
	// make sure the validation is valid

	if( theApp->getUNL().findHanko(valid.hanko()) )
	{
		mValidations[valid.hash()].push_back(valid);
	}else
	{

	}

	mMapIndexToValid[valid.ledgerindex()].push_back(valid);
}

std::vector<newcoin::Validation>* ValidationCollection::getValidations(uint64 ledgerIndex)
{
	if(mMapIndexToValid.count(ledgerIndex))
	{
		return(&(mMapIndexToValid[ledgerIndex]));
	}
	return(NULL)
}