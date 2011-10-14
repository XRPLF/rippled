#include "string"

/*
A transaction can have only one input and one output.
If you want to send an amount that is greater than any single address of yours
you must first combine coins from one address to another.
*/

class Transaction
{
	std::string mSource;
	std::string mSig;
	std::string mDest;
	unsigned int mAmount;
public:
	Transaction();
};