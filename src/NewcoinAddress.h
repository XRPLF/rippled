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
	    VER_HANKO		    = 13,
	    VER_NODE_PUBLIC	    = 18,
	    VER_NODE_PRIVATE	    = 23,
	    VER_ACCOUNT_ID	    = 0,
	    VER_ACCOUNT_PUBLIC	    = 3,
	    VER_ACCOUNT_PRIVATE	    = 8,
	    VER_FAMILY_GENERATOR    = 28,
	    VER_FAMILY_SEED	    = 33,
	} VersionEncoding;

	VersionEncoding	version;

	void seedInfo(NewcoinAddress* dstGenerator, BIGNUM** dstPrivateKey) const;

public:
	NewcoinAddress();

	bool IsValid();

	//
	// Nodes
	//
	uint160 getHanko() const;
	const std::vector<unsigned char>& getNodePublic() const;
	uint256 getNodePrivate() const;

	std::string humanHanko() const;
	std::string humanNodePublic() const;
	std::string humanNodePrivate() const;

	bool setHanko(const std::string& strHanko);
	void setHanko(const uint160& hash160);

	bool setNodePublic(const std::string& strPublic);
	void setNodePublic(const std::vector<unsigned char>& vPublic);

	bool setNodePrivate(const std::string& strPrivate);
	void setNodePrivate(uint256 hash256);

	//
	// Accounts
	//
	uint160 getAccountID() const;
	const std::vector<unsigned char>& getAccountPublic() const;
	uint256 getAccountPrivate() const;

	std::string humanAccountID() const;
	std::string humanAccountPublic() const;
	std::string humanAccountPrivate() const;

	bool setAccountID(const std::string& strAccountID);
	void setAccountID(const uint160& hash160In);

	bool setAccountPublic(const std::string& strPublic);
	void setAccountPublic(const std::vector<unsigned char>& vPublic);
	void setAccountPublic(const NewcoinAddress& generator, int seq);

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
