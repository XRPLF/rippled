#ifndef __HANKO__
#define __HANKO__

// We use SECP256K1 http://www.secg.org/collateral/sec2_final.pdf

#include "key.h"

enum HankoFormat
{
	TEXT,		// Hanko in text form
	RAW,		// Hanko in raw binary form
	CONTACT,	// Hanko contact block
};


class Hanko
{
public:
	static const int smPubKeySize= 65;
	static const int smPrivKeySize = 279;
	static const int smSigSize = 57;

private:
	std::string mHanko;
	std::vector<unsigned char> mContactBlock;
	CKey mPubKey;

public:
	Hanko();
	Hanko(const std::string& TextHanko);
	Hanko(const std::vector<unsigned char>& Data, HankoFormat format);
	Hanko(const CKey &pubKey);
	Hanko(const Hanko &);

	std::string GetHankoString(HankoFormat format) const;
        std::vector<unsigned char> GetHankoBinary(HankoFormat format) const;

        const std::vector<unsigned char>& GetContactBlock() const { return mContactBlock; }
        const CKey& GetPublicKey() const { return mPubKey; }

        int UpdateContact(std::vector<unsigned char>& Contact);
        
        bool CheckHashSign(const uint256& hash, const std::vector<unsigned char>& Signature);
        bool CheckPrefixSign(const std::vector<unsigned char>& data, uint64 type,
          const std::vector<unsigned char> &signature);
};


class LocalHanko : public Hanko
{
private:
        CKey mPrivKey;

public:
        LocalHanko(std::vector<unsigned char> &PrivKey);
        LocalHanko(const CKey &Privkey);
        LocalHanko(const LocalHanko &);
        ~LocalHanko();

        bool HashSign(const uint256& hash, std::vector<unsigned char>& Signature);
        bool PrefixSign(std::vector<unsigned char> data, uint64 type, std::vector<unsigned char> &Signature);
};

#endif
