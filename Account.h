#ifndef __ACCOUNT__
#define __ACCOUNT__

#include "uint256.h"

class Account
{
private:
    uint160 mAddress;
    CKey pubKey;

public:

  bool CheckSignRaw(const std::vector<unsigned char> &toSign,
    const std::vector<unsigned char> &signature) const;
  const uint160& GetAddress(void) const { return mAddress; }
};

#endif
