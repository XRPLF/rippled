
#include "newcoin.pb.h"
#include "types.h"

class ValidationCollection
{
	// from ledger hash to the validation
	std::map<uint256, std::vector<newcoin::Validation> > mValidations;
	std::map<uint256, std::vector<newcoin::Validation> > mIgnoredValidations;
	std::map<uint64, std::vector<newcoin::Validation> > mMapIndexToValid;
public:
	ValidationCollection();

	void addValidation(newcoin::Validation& valid);

	std::vector<newcoin::Validation>* getValidations(uint64 ledgerIndex);
};