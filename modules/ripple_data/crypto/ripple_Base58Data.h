//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
//
// Why base-58 instead of standard base-64 encoding?
// - Don't want 0OIl characters that look the same in some fonts and
//      could be used to create visually identical looking account numbers.
// - A string with non-alphanumeric characters is not as easily accepted as an account number.
// - E-mail usually won't line-break if there's no punctuation to break at.
// - Doubleclicking selects the whole number as one word if it's all alphanumeric.
//
#ifndef RIPPLE_BASE58DATA_H
#define RIPPLE_BASE58DATA_H

class CBase58Data
{
protected:
    unsigned char nVersion;
    Blob vchData;

    CBase58Data ();
    ~CBase58Data ();

    void SetData (int nVersionIn, Blob const& vchDataIn);
    void SetData (int nVersionIn, const void* pdata, size_t nSize);
    void SetData (int nVersionIn, const unsigned char* pbegin, const unsigned char* pend);

public:
    bool SetString (const char* psz, unsigned char version, const char* pAlphabet = Base58::getCurrentAlphabet ());
    bool SetString (const std::string& str, unsigned char version);

    std::string ToString () const;
    int CompareTo (const CBase58Data& b58) const;

    bool operator== (const CBase58Data& b58) const;
    bool operator!= (const CBase58Data& b58) const;
    bool operator<= (const CBase58Data& b58) const;
    bool operator>= (const CBase58Data& b58) const;
    bool operator< (const CBase58Data& b58) const;
    bool operator> (const CBase58Data& b58) const;

    friend std::size_t hash_value (const CBase58Data& b58);
};

extern std::size_t hash_value (const CBase58Data& b58);

#endif
// vim:ts=4
