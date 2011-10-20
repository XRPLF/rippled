#ifndef __VALIDATION_COLLECTION__
#define __VALIDATION_COLLECTION__

#include "newcoin.pb.h"
#include "uint256.h"
#include "types.h"

class ValidationCollection
{
	// from ledger hash to the validation
	std::map<uint256, std::vector<newcoin::Validation> > mValidations;
	std::map<uint256, std::vector<newcoin::Validation> > mIgnoredValidations;
	std::map<uint32, std::vector<newcoin::Validation> > mMapIndexToValid;

	bool hasValidation(uint256& ledgerHash,uint160& hanko);
public:
	ValidationCollection();

	void addValidation(newcoin::Validation& valid);

	std::vector<newcoin::Validation>* getValidations(uint32 ledgerIndex);
};

#endif