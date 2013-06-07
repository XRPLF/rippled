//------------------------------------------------------------------------------
/*
	Copyright (c) 2011-2013, OpenCoin, Inc.

	Permission to use, copy, modify, and/or distribute this software for any
	purpose with  or without fee is hereby granted,  provided that the above
	copyright notice and this permission notice appear in all copies.

	THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
	WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES OF
	MERCHANTABILITY  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
	ANY SPECIAL,  DIRECT, INDIRECT,  OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
	WHATSOEVER  RESULTING  FROM LOSS OF USE, DATA OR PROFITS,  WHETHER IN AN
	ACTION OF CONTRACT, NEGLIGENCE  OR OTHER TORTIOUS ACTION, ARISING OUT OF
	OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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

CBase58Data::CBase58Data()
	: nVersion(1)
{
}

CBase58Data::~CBase58Data()
{
    if (!vchData.empty())
        memset(&vchData[0], 0, vchData.size());
}

void CBase58Data::SetData(int nVersionIn, const std::vector<unsigned char>& vchDataIn)
{
	nVersion    = nVersionIn;
	vchData	    = vchDataIn;
}

void CBase58Data::SetData(int nVersionIn, const void* pdata, size_t nSize)
{
    nVersion = nVersionIn;
    vchData.resize(nSize);
    if (nSize)
        memcpy(&vchData[0], pdata, nSize);
}

void CBase58Data::SetData(int nVersionIn, const unsigned char *pbegin, const unsigned char *pend)
{
    SetData(nVersionIn, (void*)pbegin, pend - pbegin);
}

bool CBase58Data::SetString(const char* psz, unsigned char version, const char* pAlphabet)
{
    std::vector<unsigned char> vchTemp;
    Base58::decodeWithCheck (psz, vchTemp, pAlphabet);
    if (vchTemp.empty() || vchTemp[0] != version)
    {
        vchData.clear();
        nVersion = 1;
        return false;
    }
    nVersion = vchTemp[0];
    vchData.resize(vchTemp.size() - 1);
    if (!vchData.empty())
        memcpy(&vchData[0], &vchTemp[1], vchData.size());
    memset(&vchTemp[0], 0, vchTemp.size());
    return true;
}

bool CBase58Data::SetString(const std::string& str, unsigned char version)
{
    return SetString(str.c_str(), version);
}

std::string CBase58Data::ToString() const
{
    std::vector<unsigned char> vch(1, nVersion);

	vch.insert(vch.end(), vchData.begin(), vchData.end());

    return Base58::encodeWithCheck (vch);
}

int CBase58Data::CompareTo(const CBase58Data& b58) const
{
    if (nVersion < b58.nVersion) return -1;
    if (nVersion > b58.nVersion) return  1;
    if (vchData < b58.vchData)   return -1;
    if (vchData > b58.vchData)   return  1;
    return 0;
}

bool CBase58Data::operator==(const CBase58Data& b58) const { return CompareTo(b58) == 0; }
bool CBase58Data::operator!=(const CBase58Data& b58) const { return CompareTo(b58) != 0; }
bool CBase58Data::operator<=(const CBase58Data& b58) const { return CompareTo(b58) <= 0; }
bool CBase58Data::operator>=(const CBase58Data& b58) const { return CompareTo(b58) >= 0; }
bool CBase58Data::operator< (const CBase58Data& b58) const { return CompareTo(b58) <  0; }
bool CBase58Data::operator> (const CBase58Data& b58) const { return CompareTo(b58) >  0; }

std::size_t hash_value(const CBase58Data& b58)
{
	std::size_t seed = HashMaps::getInstance ().getNonce <size_t> ()
                       + (b58.nVersion * 0x9e3779b9);

    boost::hash_combine (seed, b58.vchData);
	
    return seed;
}

// vim:ts=4
