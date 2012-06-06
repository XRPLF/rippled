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
// in little-endian form

inline int Testuint256AdHoc(std::vector<std::string> vArg);

// We have to keep a separate base class without constructors
// so the compiler will let us use it in a union
template<unsigned int BITS>
class base_uint
{
protected:
	enum { WIDTH=BITS/32 };

	// This is really big-endian in byte order.
	// We use unsigned int for speed.
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

	base_uint& operator=(uint64 b)
	{
		zero();

		// Put in least significant bits.
		((uint64_t *) end())[-1]	= htobe64(b);

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
		for (int i = WIDTH-1; ++pn[i] == 0 && i; i--)
			nothing();

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
		// prefix operator
		for (int i = WIDTH-1; --pn[i] == (unsigned int) -1 && i; i--)
			nothing();

		return *this;
	}

	const base_uint operator--(int)
	{
		// postfix operator
		const base_uint ret = *this;
		--(*this);

		return ret;
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

		while (pA != pAEnd && *pA == *pB)
			pA++, pB++;

		return pA == pAEnd ? 0 : *pA < *pB ? -1 : *pA > *pB ? 1 : 0;
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
		return !compare(a, b);
	}

	friend inline bool operator!=(const base_uint& a, const base_uint& b)
	{
		return !!compare(a, b);
	}

	std::string GetHex() const
	{
		return strHex(begin(), size());
	}

	void SetHex(const char* psz)
	{
		zero();

		// skip leading spaces
		while (isspace(*psz))
			psz++;

		// skip 0x
		if (psz[0] == '0' && tolower(psz[1]) == 'x')
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

		while (phexdigit[*pEnd] >= 0)
			pEnd++;

		if ((unsigned int)(pEnd-pBegin) > 2*size())
			pBegin = pEnd - 2*size();

		unsigned char* pOut	= end()-((pEnd-pBegin+1)/2);

		while (pBegin != pEnd)
		{
			unsigned char	cHigh	= phexdigit[*pBegin++] << 4;
			unsigned char	cLow	= pBegin == pEnd
										? 0
										: phexdigit[*pBegin++];
			*pOut++	= cHigh | cLow;
		}
	}

	void SetHex(const std::string& str)
	{
		SetHex(str.c_str());
	}

	std::string ToString() const
	{
		return (GetHex());
	}

	unsigned char* begin()
	{
		return (unsigned char*) &pn[0];
	}

	unsigned char* end()
	{
		return (unsigned char*) &pn[WIDTH];
	}

	const unsigned char* begin() const
	{
		return (const unsigned char*) &pn[0];
	}

	const unsigned char* end() const
	{
		return (unsigned char*) &pn[WIDTH];
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
		uint64	uBig	= htobe64(uHost);

		zero();

		// Put in least significant bits.
		memcpy(((uint64*)end())-1, &uBig, sizeof(uBig));

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

	base_uint256 to256() const;
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

inline const std::string strHex(const uint160& ui)
{
	return strHex(ui.begin(), ui.size());
}

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

	uint256& operator=(uint64 b)
	{
		zero();

		// Put in least significant bits.
		((uint64_t *) end())[-1]	= htobe64(b);

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

	base_uint160 to160() const;
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

#endif
// vim:ts=4
