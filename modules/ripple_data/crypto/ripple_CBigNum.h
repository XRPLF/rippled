//------------------------------------------------------------------------------
//==============================================================================

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef RIPPLE_CBIGNUM_H
#define RIPPLE_CBIGNUM_H

//------------------------------------------------------------------------------

class bignum_error : public std::runtime_error
{
public:
	explicit bignum_error(const std::string& str) : std::runtime_error(str) {}
};

//------------------------------------------------------------------------------

class CAutoBN_CTX
{
private:
	CAutoBN_CTX(const CAutoBN_CTX&); // no implementation
	CAutoBN_CTX& operator=(const CAutoBN_CTX&); // no implementation

protected:
	BN_CTX* pctx;
	CAutoBN_CTX& operator=(BN_CTX* pnew) { pctx = pnew; return *this; }

public:
	CAutoBN_CTX()
	{
		pctx = BN_CTX_new();
		if (pctx == NULL)
			throw bignum_error("CAutoBN_CTX : BN_CTX_new() returned NULL");
	}

	~CAutoBN_CTX()
	{
		if (pctx != NULL)
			BN_CTX_free(pctx);
	}

	operator BN_CTX*() { return pctx; }
	BN_CTX& operator*() { return *pctx; }
	BN_CTX** operator&() { return &pctx; }
	bool operator!() { return (pctx == NULL); }
};

//------------------------------------------------------------------------------

// VFALCO: TODO figure out a way to remove the dependency on openssl in the
//		   header. Maybe rewrite this to use cryptopp.

class CBigNum : public BIGNUM
{
public:
	CBigNum();
	CBigNum(const CBigNum& b);
	CBigNum& operator=(const CBigNum& b);
	CBigNum(char n);
	CBigNum(short n);
	CBigNum(int n);
	CBigNum(long n);
	CBigNum(int64 n);
	CBigNum(unsigned char n);
	CBigNum(unsigned short n);
	CBigNum(unsigned int n);
	CBigNum(uint64 n);
	explicit CBigNum(uint256 n);
	explicit CBigNum(const std::vector<unsigned char>& vch);
	~CBigNum();

	void setuint(unsigned int n);
	unsigned int getuint() const;
	int getint() const;
	void setint64(int64 n);
	uint64 getuint64() const;
	void setuint64(uint64 n);
	void setuint256(const uint256& n);
	uint256 getuint256();
	void setvch(const std::vector<unsigned char>& vch);
	std::vector<unsigned char> getvch() const;
	CBigNum& SetCompact(unsigned int nCompact);
	unsigned int GetCompact() const;
	void SetHex(const std::string& str);
	std::string ToString(int nBase=10) const;
	std::string GetHex() const;
	bool operator!() const;
	CBigNum& operator+=(const CBigNum& b);
	CBigNum& operator-=(const CBigNum& b);
	CBigNum& operator*=(const CBigNum& b);
	CBigNum& operator/=(const CBigNum& b);
	CBigNum& operator%=(const CBigNum& b);
	CBigNum& operator<<=(unsigned int shift);
	CBigNum& operator>>=(unsigned int shift);
	CBigNum& operator++();
	CBigNum& operator--();
	const CBigNum operator++(int);
	const CBigNum operator--(int);

	friend inline const CBigNum operator-(const CBigNum& a, const CBigNum& b);
	friend inline const CBigNum operator/(const CBigNum& a, const CBigNum& b);
	friend inline const CBigNum operator%(const CBigNum& a, const CBigNum& b);

private:
	// private because the size of an unsigned long varies by platform

	void setulong(unsigned long n);
	unsigned long getulong() const;
};

const CBigNum operator+(const CBigNum& a, const CBigNum& b);
const CBigNum operator-(const CBigNum& a, const CBigNum& b);
const CBigNum operator-(const CBigNum& a);
const CBigNum operator*(const CBigNum& a, const CBigNum& b);
const CBigNum operator/(const CBigNum& a, const CBigNum& b);
const CBigNum operator%(const CBigNum& a, const CBigNum& b);
const CBigNum operator<<(const CBigNum& a, unsigned int shift);
const CBigNum operator>>(const CBigNum& a, unsigned int shift);

bool operator==(const CBigNum& a, const CBigNum& b);
bool operator!=(const CBigNum& a, const CBigNum& b);
bool operator<=(const CBigNum& a, const CBigNum& b);
bool operator>=(const CBigNum& a, const CBigNum& b);
bool operator<(const CBigNum& a, const CBigNum& b);
bool operator>(const CBigNum& a, const CBigNum& b);

//------------------------------------------------------------------------------

// VFALCO: NOTE, this seems as good a place as any for this.

// Here's the old implementation using macros, in case something broke
//#if (ULONG_MAX > UINT_MAX)
//#define BN_add_word64(bn, word) BN_add_word(bn, word)
//#define BN_sub_word64(bn, word) BN_sub_word(bn, word)
//#define BN_mul_word64(bn, word) BN_mul_word(bn, word)
//#define BN_div_word64(bn, word) BN_div_word(bn, word)
//#endif

// VFALCO: I believe only STAmount uses these
extern int BN_add_word64 (BIGNUM *a, uint64 w);
extern int BN_sub_word64 (BIGNUM *a, uint64 w);
extern int BN_mul_word64 (BIGNUM *a, uint64 w);
extern uint64 BN_div_word64 (BIGNUM *a, uint64 w);

#endif

// vim:ts=4
