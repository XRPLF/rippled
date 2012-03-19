#ifndef __NEWCOIN_ADDRESS__
#define __NEWCOIN_ADDRESS__

#include "base58.h"
#include "uint256.h"

//
// Used to hold addresses and parse and produce human formats.
//
class NewcoinAddress : public CBase58Data
{
private:
	typedef enum {
	    VER_NONE		    = 1,
	    VER_HANKO		    = 8,
	    VER_NODE_PUBLIC	    = 28,
	    VER_NODE_PRIVATE	    = 32,
	    VER_ACCOUNT_ID	    = 0,
	    VER_ACCOUNT_PUBLIC	    = 35,
	    VER_ACCOUNT_PRIVATE	    = 34,
	    VER_FAMILY_GENERATOR    = 41,
	    VER_FAMILY_SEED	    = 33,
	} VersionEncoding;

	void seedInfo(NewcoinAddress* dstGenerator, BIGNUM** dstPrivateKey) const;

public:
	NewcoinAddress();

	bool IsValid();

	//
	// hanko
	//
	uint160 getHanko() const;

	std::string humanHanko() const;

	bool setHanko(const std::string& strHanko);
	void setHanko(const uint160& hash160);

	//
	// Node Public
	//
	const std::vector<unsigned char>& getNodePublic() const;

	std::string humanNodePublic() const;

	bool setNodePublic(const std::string& strPublic);
	void setNodePublic(const std::vector<unsigned char>& vPublic);

	//
	// Node Private
	//
	uint256 getNodePrivate() const;

	std::string humanNodePrivate() const;

	bool setNodePrivate(const std::string& strPrivate);
	void setNodePrivate(uint256 hash256);

	//
	// Accounts IDs
	//
	uint160 getAccountID() const;

	std::string humanAccountID() const;

	bool setAccountID(const std::string& strAccountID);
	void setAccountID(const uint160& hash160In);

	//
	// Accounts Public
	//
	const std::vector<unsigned char>& getAccountPublic() const;

	std::string humanAccountPublic() const;

	bool setAccountPublic(const std::string& strPublic);
	void setAccountPublic(const std::vector<unsigned char>& vPublic);
	void setAccountPublic(const NewcoinAddress& generator, int seq);

	//
	// Accounts Private
	//
	uint256 getAccountPrivate() const;

	std::string humanAccountPrivate() const;

	bool setAccountPrivate(const std::string& strPrivate);
	void setAccountPrivate(uint256 hash256);

	//
	// Family Generators
	//
	BIGNUM* getFamilyGeneratorBN() const;
	const std::vector<unsigned char>& getFamilyGenerator() const;

	std::string humanFamilyGenerator() const;

	bool setFamilyGenerator(const std::string& strGenerator);
	void setFamilyGenerator(const std::vector<unsigned char>& vPublic);
	void setFamilyGenerator(const NewcoinAddress& seed);

	//
	// Family Seeds
	//
	uint128 getFamilySeed() const;
	BIGNUM*	getFamilyPrivateKey() const;

	std::string humanFamilySeed() const;

	bool setFamilySeed(const std::string& strSeed);
	void setFamilySeed(uint128 hash128);
};

#endif
