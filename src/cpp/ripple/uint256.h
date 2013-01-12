// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
#ifndef NEWCOIN_UINT256_H
#define NEWCOIN_UINT256_H

#include <algorithm>
#include <climits>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cassert>

#include <boost/functional/hash.hpp>

#include "types.h"
#include "utils.h"

#if defined(_MSC_VER) && _MSC_VER < 1300
#define for  if (false) ; else for
#endif

// These classes all store their values internally
// in big-endian form

inline int Testuint256AdHoc(std::vector<std::string> vArg);

// We have to keep a separate base class without constructors
// so the compiler will let us use it in a union
template<unsigned int BITS>
class base_uint
{
protected:
	enum { WIDTH=BITS/32 };

	// This is really big-endian in byte order.
	// We sometimes use unsigned int for speed.
	unsigned int pn[WIDTH];

public:
	bool isZero() const
	{
		for (int i = 0; i < WIDTH; i++)
			if (pn[i] != 0)
				return false;
		return true;
	}

	bool isNonZero() const
	{
		return !isZero();
	}

	bool operator!() const
	{
		return isZero();
	}

	const base_uint operator~() const
	{
		base_uint ret;

		for (int i = 0; i < WIDTH; i++)
			ret.pn[i] = ~pn[i];

		return ret;
	}

	base_uint& operator=(uint64 uHost)
	{
		zero();

		// Put in least significant bits.
		((uint64_t *) end())[-1] = htobe64(uHost);

		return *this;
	}

	base_uint& operator^=(const base_uint& b)
	{
		for (int i = 0; i < WIDTH; i++)
			pn[i] ^= b.pn[i];

		return *this;
	}

	base_uint& operator&=(const base_uint& b)
	{
		for (int i = 0; i < WIDTH; i++)
			pn[i] &= b.pn[i];

		return *this;
	}

	base_uint& operator|=(const base_uint& b)
	{
		for (int i = 0; i < WIDTH; i++)
			pn[i] |= b.pn[i];

		return *this;
	}

	base_uint& operator++()
	{
		// prefix operator
		for (int i = WIDTH - 1; i >= 0; --i)
		{
			pn[i] = htobe32(be32toh(pn[i]) + 1);
			if (pn[i] != 0)
				break;
		}

		return *this;
	}

	const base_uint operator++(int)
	{
		// postfix operator
		const base_uint ret = *this;
		++(*this);

		return ret;
	}

	base_uint& operator--()
	{
		for (int i = WIDTH - 1; i >= 0; --i)
		{
			uint32 prev = pn[i];
			pn[i] = htobe32(be32toh(pn[i]) - 1);
			if (prev != 0)
				break;
		}

		return *this;
	}

	const base_uint operator--(int)
	{
		// postfix operator
		const base_uint ret = *this;
		--(*this);

		return ret;
	}

	base_uint& operator+=(const base_uint& b)
	{
		uint64 carry = 0;

		for (int i = WIDTH; i--;)
		{
			uint64 n = carry + be32toh(pn[i]) + be32toh(b.pn[i]);

			pn[i] = htobe32(n & 0xffffffff);
			carry = n >> 32;
		}

		return *this;
	}

	std::size_t hash_combine(std::size_t& seed) const
	{
		for (int i = 0; i < WIDTH; ++i)
			boost::hash_combine(seed, pn[i]);
		return seed;
	}

	friend inline int compare(const base_uint& a, const base_uint& b)
	{
		const unsigned char* pA		= a.begin();
		const unsigned char* pAEnd	= a.end();
		const unsigned char* pB		= b.begin();

		while (*pA == *pB)
		{
			if (++pA == pAEnd)
				return 0;
			++pB;
		}

		return (*pA < *pB) ? -1 : 1;
	}

	friend inline bool operator<(const base_uint& a, const base_uint& b)
	{
		return compare(a, b) < 0;
	}

	friend inline bool operator<=(const base_uint& a, const base_uint& b)
	{
		return compare(a, b) <= 0;
	}

	friend inline bool operator>(const base_uint& a, const base_uint& b)
	{
		return compare(a, b) > 0;
	}

	friend inline bool operator>=(const base_uint& a, const base_uint& b)
	{
		return compare(a, b) >= 0;
	}

	friend inline bool operator==(const base_uint& a, const base_uint& b)
	{
		return memcmp(a.pn, b.pn, sizeof(a.pn)) == 0;
	}

	friend inline bool operator!=(const base_uint& a, const base_uint& b)
	{
		return memcmp(a.pn, b.pn, sizeof(a.pn)) != 0;
	}

	std::string GetHex() const
	{
		return strHex(begin(), size());
	}

	// Allow leading whitespace.
	// Allow leading "0x".
	// To be valid must be '\0' terminated.
	bool SetHex(const char* psz, bool bStrict=false)
	{
		// skip leading spaces
		if (!bStrict)
			while (isspace(*psz))
				psz++;

		// skip 0x
		if (!bStrict && psz[0] == '0' && tolower(psz[1]) == 'x')
			psz += 2;

		// hex char to int
		static signed char phexdigit[256] = {
			-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
			-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
			-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
			 0, 1, 2, 3,  4, 5, 6, 7,  8, 9,-1,-1, -1,-1,-1,-1,

			-1,0xa,0xb,0xc, 0xd,0xe,0xf,-1, -1,-1,-1,-1, -1,-1,-1,-1,
			-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
			-1,0xa,0xb,0xc, 0xd,0xe,0xf,-1, -1,-1,-1,-1, -1,-1,-1,-1,
			-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,

			-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
			-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
			-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
			-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,

			-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
			-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
			-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
			-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
			};

		const unsigned char* pEnd	= reinterpret_cast<const unsigned char*>(psz);
		const unsigned char* pBegin = pEnd;

		// Find end.
		while (phexdigit[*pEnd] >= 0)
			pEnd++;

		// Take only last digits of over long string.
		if ((unsigned int)(pEnd-pBegin) > 2*size())
			pBegin = pEnd - 2*size();

		unsigned char* pOut	= end()-((pEnd-pBegin+1)/2);

		zero();

		if ((pEnd-pBegin) & 1)
			*pOut++	= phexdigit[*pBegin++];

		while (pBegin != pEnd)
		{
			unsigned char	cHigh	= phexdigit[*pBegin++] << 4;
			unsigned char	cLow	= pBegin == pEnd
										? 0
										: phexdigit[*pBegin++];
			*pOut++	= cHigh | cLow;
		}

		return !*pEnd;
	}

	bool SetHex(const std::string& str, bool bStrict=false)
	{
		return SetHex(str.c_str(), bStrict);
	}

	std::string ToString() const
	{
		return GetHex();
	}

	unsigned char* begin()
	{
		return reinterpret_cast<unsigned char*>(pn);
	}

	unsigned char* end()
	{
		return reinterpret_cast<unsigned char*>(pn + WIDTH);
	}

	const unsigned char* begin() const
	{
		return reinterpret_cast<const unsigned char*>(pn);
	}

	const unsigned char* end() const
	{
		return reinterpret_cast<const unsigned char*>(pn + WIDTH);
	}

	unsigned int size() const
	{
		return sizeof(pn);
	}

	void zero()
	{
		memset(&pn[0], 0, sizeof(pn));
	}

	unsigned int GetSerializeSize(int nType=0) const
	{
		return sizeof(pn);
	}

	template<typename Stream>
	void Serialize(Stream& s, int nType=0) const
	{
		s.write((char*)pn, sizeof(pn));
	}

	template<typename Stream>
	void Unserialize(Stream& s, int nType=0)
	{
		s.read((char*)pn, sizeof(pn));
	}

	friend class uint128;
	friend class uint160;
	friend class uint256;
	friend inline int Testuint256AdHoc(std::vector<std::string> vArg);
};

typedef base_uint<128> base_uint128;
typedef base_uint<160> base_uint160;
typedef base_uint<256> base_uint256;

//
// uint128, uint160, & uint256 could be implemented as templates, but to keep
// compile errors and debugging cleaner, they're copy and pasted.
//

//////////////////////////////////////////////////////////////////////////////
//
// uint128
//

class uint128 : public base_uint128
{
public:
	typedef base_uint128 basetype;

	uint128()
	{
		zero();
	}

	uint128(const basetype& b)
	{
		*this = b;
	}

	uint128& operator=(const basetype& b)
	{
		for (int i = 0; i < WIDTH; i++)
			pn[i] = b.pn[i];

		return *this;
	}

	explicit uint128(const base_uint256& b) {
		for (int i = 0; i < WIDTH; i++)
			pn[i] = b.pn[i];
	}

	explicit uint128(const std::vector<unsigned char>& vch)
	{
		if (vch.size() == size())
			memcpy(pn, &vch[0], size());
		else
			zero();
	}

};

//////////////////////////////////////////////////////////////////////////////
//
// uint256
//

class uint256 : public base_uint256
{
public:
	typedef base_uint256 basetype;

	uint256()
	{
		zero();
	}

	uint256(const basetype& b)
	{
		*this	= b;
	}

	uint256& operator=(const basetype& b)
	{
		for (int i = 0; i < WIDTH; i++)
			pn[i] = b.pn[i];

		return *this;
	}

	uint256(uint64 b)
	{
		*this = b;
	}

	uint256& operator=(uint64 uHost)
	{
		zero();

		// Put in least significant bits.
		((uint64_t *) end())[-1]	= htobe64(uHost);

		return *this;
	}

	explicit uint256(const std::string& str)
	{
		SetHex(str);
	}

	explicit uint256(const std::vector<unsigned char>& vch)
	{
		if (vch.size() == sizeof(pn))
			memcpy(pn, &vch[0], sizeof(pn));
		else
		{
			assert(false);
			zero();
		}
	}
};


inline bool operator==(const uint256& a, uint64 b)						   { return (base_uint256)a == b; }
inline bool operator!=(const uint256& a, uint64 b)						   { return (base_uint256)a != b; }
inline const uint256 operator^(const base_uint256& a, const base_uint256& b) { return uint256(a) ^= b; }
inline const uint256 operator&(const base_uint256& a, const base_uint256& b) { return uint256(a) &= b; }
inline const uint256 operator|(const base_uint256& a, const base_uint256& b) { return uint256(a) |= b; }
inline bool operator==(const base_uint256& a, const uint256& b)		 { return (base_uint256)a == (base_uint256)b; }
inline bool operator!=(const base_uint256& a, const uint256& b)		 { return (base_uint256)a != (base_uint256)b; }
inline const uint256 operator^(const base_uint256& a, const uint256& b) { return (base_uint256)a ^  (base_uint256)b; }
inline const uint256 operator&(const base_uint256& a, const uint256& b) { return (base_uint256)a &  (base_uint256)b; }
inline const uint256 operator|(const base_uint256& a, const uint256& b) { return (base_uint256)a |  (base_uint256)b; }
inline bool operator==(const uint256& a, const base_uint256& b)		 { return (base_uint256)a == (base_uint256)b; }
inline bool operator!=(const uint256& a, const base_uint256& b)		 { return (base_uint256)a != (base_uint256)b; }
inline const uint256 operator^(const uint256& a, const base_uint256& b) { return (base_uint256)a ^  (base_uint256)b; }
inline const uint256 operator&(const uint256& a, const base_uint256& b) { return (base_uint256)a &  (base_uint256)b; }
inline const uint256 operator|(const uint256& a, const base_uint256& b) { return (base_uint256)a |  (base_uint256)b; }
inline bool operator==(const uint256& a, const uint256& b)			  { return (base_uint256)a == (base_uint256)b; }
inline bool operator!=(const uint256& a, const uint256& b)			  { return (base_uint256)a != (base_uint256)b; }
inline const uint256 operator^(const uint256& a, const uint256& b)	  { return (base_uint256)a ^  (base_uint256)b; }
inline const uint256 operator&(const uint256& a, const uint256& b)	  { return (base_uint256)a &  (base_uint256)b; }
inline const uint256 operator|(const uint256& a, const uint256& b)	  { return (base_uint256)a |  (base_uint256)b; }
extern std::size_t hash_value(const uint256&);

template<unsigned int BITS> inline std::ostream& operator<<(std::ostream& out, const base_uint<BITS>& u)
{
	return out << u.GetHex();
}

inline int Testuint256AdHoc(std::vector<std::string> vArg)
{
	uint256 g(0);

	printf("%s\n", g.ToString().c_str());
	--g;  printf("--g\n");
	printf("%s\n", g.ToString().c_str());
	g--;  printf("g--\n");
	printf("%s\n", g.ToString().c_str());
	g++;  printf("g++\n");
	printf("%s\n", g.ToString().c_str());
	++g;  printf("++g\n");
	printf("%s\n", g.ToString().c_str());
	g++;  printf("g++\n");
	printf("%s\n", g.ToString().c_str());
	++g;  printf("++g\n");
	printf("%s\n", g.ToString().c_str());



	uint256 a(7);
	printf("a=7\n");
	printf("%s\n", a.ToString().c_str());

	uint256 b;
	printf("b undefined\n");
	printf("%s\n", b.ToString().c_str());
	int c = 3;

	a = c;
	a.pn[3] = 15;
	printf("%s\n", a.ToString().c_str());
	uint256 k(c);

	a = 5;
	a.pn[3] = 15;
	printf("%s\n", a.ToString().c_str());
	b = 1;
	// b <<= 52;

	a |= b;

	// a ^= 0x500;

	printf("a %s\n", a.ToString().c_str());

	a = a | b | (uint256)0x1000;


	printf("a %s\n", a.ToString().c_str());
	printf("b %s\n", b.ToString().c_str());

	a = 0xfffffffe;
	a.pn[4] = 9;

	printf("%s\n", a.ToString().c_str());
	a++;
	printf("%s\n", a.ToString().c_str());
	a++;
	printf("%s\n", a.ToString().c_str());
	a++;
	printf("%s\n", a.ToString().c_str());
	a++;
	printf("%s\n", a.ToString().c_str());

	a--;
	printf("%s\n", a.ToString().c_str());
	a--;
	printf("%s\n", a.ToString().c_str());
	a--;
	printf("%s\n", a.ToString().c_str());
	uint256 d = a--;
	printf("%s\n", d.ToString().c_str());
	printf("%s\n", a.ToString().c_str());
	a--;
	printf("%s\n", a.ToString().c_str());
	a--;
	printf("%s\n", a.ToString().c_str());

	d = a;

	printf("%s\n", d.ToString().c_str());
	for (int i = uint256::WIDTH-1; i >= 0; i--) printf("%08x", d.pn[i]); printf("\n");

	uint256 neg = d;
	neg = ~neg;
	printf("%s\n", neg.ToString().c_str());


	uint256 e = uint256("0xABCDEF123abcdef12345678909832180000011111111");
	printf("\n");
	printf("%s\n", e.ToString().c_str());


	printf("\n");
	uint256 x1 = uint256("0xABCDEF123abcdef12345678909832180000011111111");
	uint256 x2;
	printf("%s\n", x1.ToString().c_str());
	for (int i = 0; i < 270; i += 4)
	{
		// x2 = x1 << i;
		printf("%s\n", x2.ToString().c_str());
	}

	printf("\n");
	printf("%s\n", x1.ToString().c_str());
	for (int i = 0; i < 270; i += 4)
	{
		x2 = x1;
		// x2 >>= i;
		printf("%s\n", x2.ToString().c_str());
	}

	#if 0
	for (int i = 0; i < 100; i++)
	{
		uint256 k = (~uint256(0) >> i);
		printf("%s\n", k.ToString().c_str());
	}

	for (int i = 0; i < 100; i++)
	{
		uint256 k = (~uint256(0) << i);
		printf("%s\n", k.ToString().c_str());
	}
	#endif

	return (0);
}

//////////////////////////////////////////////////////////////////////////////
//
// uint160
//

class uint160 : public base_uint160
{
public:
	typedef base_uint160 basetype;

	uint160()
	{
		zero();
	}

	uint160(const basetype& b)
	{
		*this	= b;
	}

	uint160& operator=(const basetype& b)
	{
		for (int i = 0; i < WIDTH; i++)
			pn[i] = b.pn[i];

		return *this;
	}

	uint160(uint64 b)
	{
		*this = b;
	}

	uint160& operator=(uint64 uHost)
	{
		zero();

		// Put in least significant bits.
		((uint64_t *) end())[-1]	= htobe64(uHost);

		return *this;
	}

	explicit uint160(const std::string& str)
	{
		SetHex(str);
	}

	explicit uint160(const std::vector<unsigned char>& vch)
	{
		if (vch.size() == sizeof(pn))
			memcpy(pn, &vch[0], sizeof(pn));
		else
			zero();
	}

	base_uint256 to256() const
	{
	  uint256 m;
	  memcpy(m.begin(), begin(), size());
	  return m;
	}

};

inline bool operator==(const uint160& a, uint64 b)						   { return (base_uint160)a == b; }
inline bool operator!=(const uint160& a, uint64 b)						   { return (base_uint160)a != b; }

inline const uint160 operator^(const base_uint160& a, const base_uint160& b) { return uint160(a) ^= b; }
inline const uint160 operator&(const base_uint160& a, const base_uint160& b) { return uint160(a) &= b; }
inline const uint160 operator|(const base_uint160& a, const base_uint160& b) { return uint160(a) |= b; }

inline bool operator==(const base_uint160& a, const uint160& b)		 { return (base_uint160)a == (base_uint160)b; }
inline bool operator!=(const base_uint160& a, const uint160& b)		 { return (base_uint160)a != (base_uint160)b; }
inline const uint160 operator^(const base_uint160& a, const uint160& b) { return (base_uint160)a ^  (base_uint160)b; }
inline const uint160 operator&(const base_uint160& a, const uint160& b) { return (base_uint160)a &  (base_uint160)b; }
inline const uint160 operator|(const base_uint160& a, const uint160& b) { return (base_uint160)a |  (base_uint160)b; }

inline bool operator==(const uint160& a, const base_uint160& b)		 { return (base_uint160)a == (base_uint160)b; }
inline bool operator!=(const uint160& a, const base_uint160& b)		 { return (base_uint160)a != (base_uint160)b; }
inline const uint160 operator^(const uint160& a, const base_uint160& b) { return (base_uint160)a ^  (base_uint160)b; }
inline const uint160 operator&(const uint160& a, const base_uint160& b) { return (base_uint160)a &  (base_uint160)b; }
inline const uint160 operator|(const uint160& a, const base_uint160& b) { return (base_uint160)a |  (base_uint160)b; }
inline bool operator==(const uint160& a, const uint160& b)			  { return (base_uint160)a == (base_uint160)b; }
inline bool operator!=(const uint160& a, const uint160& b)			  { return (base_uint160)a != (base_uint160)b; }
inline const uint160 operator^(const uint160& a, const uint160& b)	  { return (base_uint160)a ^  (base_uint160)b; }
inline const uint160 operator&(const uint160& a, const uint160& b)	  { return (base_uint160)a &  (base_uint160)b; }
inline const uint160 operator|(const uint160& a, const uint160& b)	  { return (base_uint160)a |  (base_uint160)b; }

extern std::size_t hash_value(const uint160&);

inline const std::string strHex(const uint160& ui)
{
	return strHex(ui.begin(), ui.size());
}


#endif
// vim:ts=4
