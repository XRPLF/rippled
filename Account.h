#ifndef __ACCOUNT__
#define __ACCOUNT__

#include <boost/shared_ptr.hpp>
#include "key.h"
#include "uint256.h"

class Account
{
public:
	typedef boost::shared_ptr<Account> pointer;

private:
    uint160 mAddress;
    CKey pubKey;

public:

  bool checkSignRaw(const std::vector<unsigned char> &toSign,
    const std::vector<unsigned char> &signature) const;
  const uint160& getAddress(void) const { return mAddress; }
  CKey& peekPubKey() { return pubKey; }
};

#endif
