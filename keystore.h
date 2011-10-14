// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KEYSTORE_H
#define BITCOIN_KEYSTORE_H

#include "key.h"
//#include "crypter.h"
#include <map>

class CKeyStore
{
protected:
   

public:
    virtual bool AddKey(const CKey& key) =0;
    virtual bool HaveKey(const NewcoinAddress &address) const =0;
    virtual bool GetKey(const NewcoinAddress &address, CKey& keyOut) const =0;
    virtual bool GetPubKey(const NewcoinAddress &address, std::vector<unsigned char>& vchPubKeyOut) const;
    virtual std::vector<unsigned char> GenerateNewKey();
};

typedef std::map<NewcoinAddress, CSecret> KeyMap;

class CBasicKeyStore : public CKeyStore
{
protected:
    KeyMap mapKeys;

public:
    bool AddKey(const CKey& key);
    bool HaveKey(const NewcoinAddress &address) const
    {
        bool result;
        
        result = (mapKeys.count(address) > 0);
        return result;
    }
    bool GetKey(const NewcoinAddress &address, CKey& keyOut) const
    {
        
        {
            KeyMap::const_iterator mi = mapKeys.find(address);
            if (mi != mapKeys.end())
            {
                keyOut.SetSecret((*mi).second);
                return true;
            }
        }
        return false;
    }
};



#endif
