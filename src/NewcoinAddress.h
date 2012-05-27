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
	    VER_NONE				= 1,
	    VER_NODE_PUBLIC			= 28,
	    VER_NODE_PRIVATE	    = 32,
	    VER_ACCOUNT_ID			= 0,
	    VER_ACCOUNT_PUBLIC	    = 35,
	    VER_ACCOUNT_PRIVATE	    = 34,
	    VER_FAMILY_GENERATOR    = 41,
	    VER_FAMILY_SEED			= 33,
	} VersionEncoding;

	void seedInfo(NewcoinAddress* dstGenerator, BIGNUM** dstPrivateKey) const;

public:
	NewcoinAddress();

	// For public and private key, checks if they are legal.
	bool isValid() const;
	void clear();

	//
	// Node Public
	//
	const std::vector<unsigned char>& getNodePublic() const;

	std::string humanNodePublic() const;

	bool setNodePublic(const std::string& strPublic);
	void setNodePublic(const std::vector<unsigned char>& vPublic);
	bool verifyNodePublic(const uint256& hash, const std::vector<unsigned char>& vchSig) const;
	bool verifyNodePublic(const uint256& hash, const std::string& strSig) const;

	//
	// Node Private
	//
	const std::vector<unsigned char>& getNodePrivateData() const;
	uint256 getNodePrivate() const;

	std::string humanNodePrivate() const;

	bool setNodePrivate(const std::string& strPrivate);
	void setNodePrivate(const std::vector<unsigned char>& vPrivate);
	void setNodePrivate(uint256 hash256);
	void signNodePrivate(const uint256& hash, std::vector<unsigned char>& vchSig) const;

	//
	// Accounts IDs
	//
	uint160 getAccountID() const;

	std::string humanAccountID() const;

	bool setAccountID(const std::string& strAccountID);
	void setAccountID(const uint160& hash160In);

	static NewcoinAddress createAccountID(const uint160& uiAccountID);

	//
	// Accounts Public
	//
	const std::vector<unsigned char>& getAccountPublic() const;

	std::string humanAccountPublic() const;

	bool setAccountPublic(const std::string& strPublic);
	void setAccountPublic(const std::vector<unsigned char>& vPublic);
	void setAccountPublic(const NewcoinAddress& generator, int seq);

	bool accountPublicVerify(const uint256& uHash, const std::vector<unsigned char>& vucSig) const;

	//
	// Accounts Private
	//
	const std::vector<unsigned char>& getAccountPrivate() const;

	std::string humanAccountPrivate() const;

	bool setAccountPrivate(const std::string& strPrivate);
	void setAccountPrivate(const std::vector<unsigned char>& vPrivate);
	void setAccountPrivate(uint256 hash256);
	void setAccountPrivate(const NewcoinAddress& generator, const NewcoinAddress& seed, int seq);

	bool accountPrivateSign(const uint256& uHash, std::vector<unsigned char>& vucSig) const;
	bool accountPrivateVerify(const uint256& uHash, const std::vector<unsigned char>& vucSig) const;

	// Encrypt a message.
	std::vector<unsigned char> accountPrivateEncrypt(const NewcoinAddress& naPublicTo, const std::vector<unsigned char>& vucPlainText) const;

	// Decrypt a message.
	std::vector<unsigned char> accountPrivateDecrypt(const NewcoinAddress& naPublicFrom, const std::vector<unsigned char>& vucCipherText) const;

	//
	// Family Generators
	// Use to generate a master or regular family.
	//
	BIGNUM* getFamilyGeneratorBN() const;
	const std::vector<unsigned char>& getFamilyGenerator() const;

	std::string humanFamilyGenerator() const;

	bool setFamilyGenerator(const std::string& strGenerator);
	void setFamilyGenerator(const std::vector<unsigned char>& vPublic);
	void setFamilyGenerator(const NewcoinAddress& seed);

	//
	// Family Seeds
	// Clients must disallow reconizable entries from being seeds.
	uint128 getFamilySeed() const;
	BIGNUM*	getFamilyPrivateKey() const;

	std::string humanFamilySeed() const;
	std::string humanFamilySeed1751() const;

	bool setFamilySeed(const std::string& strSeed);
	int setFamilySeed1751(const std::string& strHuman1751);
	bool setFamilySeedGeneric(const std::string& strText);
	void setFamilySeed(uint128 hash128);
	void setFamilySeedRandom();
};

#endif
// vim:ts=4
