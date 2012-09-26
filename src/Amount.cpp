#include <cmath>
#include <iomanip>
#include <algorithm>

#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

#include "Config.h"
#include "Log.h"
#include "SerializedTypes.h"
#include "utils.h"

uint64	STAmount::uRateOne	= STAmount::getRate(STAmount(1), STAmount(1));

bool STAmount::currencyFromString(uint160& uDstCurrency, const std::string& sCurrency)
{
	bool	bSuccess	= true;

	if (sCurrency.empty() || !sCurrency.compare(SYSTEM_CURRENCY_CODE))
	{
		uDstCurrency.zero();
	}
	else if (3 == sCurrency.size())
	{
		std::vector<unsigned char>	vucIso(3);

		std::transform(sCurrency.begin(), sCurrency.end(), vucIso.begin(), ::toupper);

		// std::string	sIso;
		// sIso.assign(vucIso.begin(), vucIso.end());
		// std::cerr << "currency: " << sIso << std::endl;

		Serializer	s;

		s.addZeros(96/8);
		s.addRaw(vucIso);
		s.addZeros(16/8);
		s.addZeros(24/8);

		s.get160(uDstCurrency, 0);
	}
	else
	{
		bSuccess	= false;
	}

	return bSuccess;
}

// XXX Broken for custom currencies?
std::string STAmount::getHumanCurrency() const
{
	return createHumanCurrency(mCurrency);
}

std::string STAmount::createHumanCurrency(const uint160& uCurrency)
{
	std::string	sCurrency;

	if (uCurrency.isZero())
	{
		return SYSTEM_CURRENCY_CODE;
	}
	else if (uCurrency == CURRENCY_ONE)
	{
		return "1";
	}
	else
	{
		Serializer	s(160/8);

		s.add160(uCurrency);

		SerializerIterator	sit(s);

		std::vector<unsigned char>	vucZeros	= sit.getRaw(96/8);
		std::vector<unsigned char>	vucIso		= sit.getRaw(24/8);
		std::vector<unsigned char>	vucVersion	= sit.getRaw(16/8);
		std::vector<unsigned char>	vucReserved	= sit.getRaw(24/8);

		if (!::isZero(vucZeros.begin(), vucZeros.size()))
		{
			throw std::runtime_error(boost::str(boost::format("bad currency: zeros: %s") % uCurrency));
		}
		else if (!::isZero(vucVersion.begin(), vucVersion.size()))
		{
			throw std::runtime_error(boost::str(boost::format("bad currency: version: %s") % uCurrency));
		}
		else if (!::isZero(vucReserved.begin(), vucReserved.size()))
		{
			throw std::runtime_error(boost::str(boost::format("bad currency: reserved: %s") % uCurrency));
		}
		else
		{
			sCurrency.assign(vucIso.begin(), vucIso.end());
		}
	}

	return sCurrency;
}

// Not meant to be the ultimate parser.  For use by RPC which is supposed to be sane and trusted.
// Native has special handling:
// - Integer values are in base units.
// - Float values are in float units.
// - To avoid a mistake float value for native are specified with a "^" in place of a "."
// <-- bValid: true = valid
bool STAmount::setFullValue(const std::string& sAmount, const std::string& sCurrency, const std::string& sIssuer)
{
	//
	// Figure out the currency.
	//
	if (!currencyFromString(mCurrency, sCurrency))
	{
		Log(lsINFO) << "Currency malformed: " << sCurrency;

		return false;
	}

	mIsNative	= !mCurrency;

	//
	// Figure out the issuer.
	//
	NewcoinAddress	naIssuerID;

	// Issuer must be "" or a valid account string.
	if (!naIssuerID.setAccountID(sIssuer))
	{
		Log(lsINFO) << "Issuer malformed: " << sIssuer;

		return false;
	}

	mIssuer	= naIssuerID.getAccountID();

	// Stamps not must have an issuer.
	if (mIsNative && !mIssuer.isZero())
	{
		Log(lsINFO) << "Issuer specified for stamps: " << sIssuer;

		return false;
	}

	uint64	uValue;
	int		iOffset;
	size_t	uDecimal	= sAmount.find_first_of(mIsNative ? "^" : ".");
	bool	bInteger	= uDecimal == std::string::npos;

	if (bInteger)
	{
		try
		{
			uValue	= sAmount.empty() ? 0 : boost::lexical_cast<uint64>(sAmount);
		}
		catch (...)
		{
			Log(lsINFO) << "Bad integer amount: " << sAmount;

			return false;
		}
		iOffset	= 0;
	}
	else
	{
		// Example size decimal size-decimal offset
		//    ^1      2       0            2     -1
		// 123^       4       3            1      0
		//   1^23     4       1            3     -2
		iOffset	= -int(sAmount.size()-uDecimal-1);


		// Issolate integer and fraction.
		uint64	uInteger	= uDecimal ? boost::lexical_cast<uint64>(sAmount.substr(0, uDecimal)) : 0;
		uint64	uFraction	= iOffset ? boost::lexical_cast<uint64>(sAmount.substr(uDecimal+1)) : 0;

		// Scale the integer portion to the same offset as the fraction.
		uValue	= uInteger;
		for (int i=-iOffset; i--;)
			uValue	*= 10;

		// Add in the fraction.
		uValue += uFraction;
	}

	if (mIsNative)
	{
		if (bInteger)
			iOffset	= -SYSTEM_CURRENCY_PRECISION;

		while (iOffset > -SYSTEM_CURRENCY_PRECISION) {
			uValue	*= 10;
			--iOffset;
		}

		while (iOffset < -SYSTEM_CURRENCY_PRECISION) {
			uValue	/= 10;
			++iOffset;
		}

		mValue		= uValue;
	}
	else
	{
		mValue		= uValue;
		mOffset		= iOffset;
		canonicalize();
	}

	return true;
}

// amount = value * [10 ^ offset]
// representation range is 10^80 - 10^(-80)
// on the wire, high 8 bits are (offset+142), low 56 bits are value
// value is zero if amount is zero, otherwise value is 10^15 to (10^16 - 1) inclusive

void STAmount::canonicalize()
{
	if (!mCurrency)
	{ // native currency amounts should always have an offset of zero
		mIsNative = true;

		if (mValue == 0)
		{
			mOffset = 0;
			mIsNegative = false;
			return;
		}

		while (mOffset < 0)
		{
			mValue /= 10;
			++mOffset;
		}

		while (mOffset > 0)
		{
			mValue *= 10;
			--mOffset;
		}

		assert(mValue <= cMaxNative);
		return;
	}

	mIsNative = false;
	if (mValue == 0)
	{
		mOffset = -100;
		mIsNegative = false;
		return;
	}

	while (mValue < cMinValue)
	{
		if (mOffset <= cMinOffset)
			throw std::runtime_error("value overflow");
		mValue *= 10;
		if (mValue >= cMaxValue)
			throw std::runtime_error("value overflow");
		--mOffset;
	}

	while (mValue > cMaxValue)
	{
		if (mOffset >= cMaxOffset)
			throw std::runtime_error("value underflow");
		mValue /= 10;
		++mOffset;
	}
	assert((mValue == 0) || ((mValue >= cMinValue) && (mValue <= cMaxValue)) );
	assert((mValue == 0) || ((mOffset >= cMinOffset) && (mOffset <= cMaxOffset)) );
	assert((mValue != 0) || (mOffset != -100) );
}

void STAmount::add(Serializer& s) const
{
	if (mIsNative)
	{
		assert(mOffset == 0);
		if (!mIsNegative)
			s.add64(mValue | cPosNative);
		else
			s.add64(mValue);
	}
	else
	{
		if (isZero())
			s.add64(cNotNative);
		else if (mIsNegative) // 512 = not native
			s.add64(mValue | (static_cast<uint64>(mOffset + 512 + 97) << (64 - 10)));
		else // 256 = positive
			s.add64(mValue | (static_cast<uint64>(mOffset + 512 + 256 + 97) << (64 - 10)));

		s.add160(mCurrency);
		s.add160(mIssuer);
	}
}

STAmount::STAmount(SField::ref name, int64 value) : SerializedType(name), mOffset(0), mIsNative(true)
{
	if (value >= 0)
	{
		mIsNegative	= false;
		mValue		= static_cast<uint64>(value);
	}
	else
	{
		mIsNegative	= true;
		mValue		= static_cast<uint64>(-value);
	}
}

void STAmount::setValue(const STAmount &a)
{
	mCurrency	= a.mCurrency;
	mIssuer		= a.mIssuer;
	mValue		= a.mValue;
	mOffset		= a.mOffset;
	mIsNative	= a.mIsNative;
	mIsNegative = a.mIsNegative;
}

uint64 STAmount::toUInt64() const
{ // makes them sort easily
	if (mValue == 0) return 0x4000000000000000ull;
	if (mIsNegative)
		return mValue | (static_cast<uint64>(mOffset + 97) << (64 - 10));
	return mValue | (static_cast<uint64>(mOffset + 256 + 97) << (64 - 10));
}

STAmount* STAmount::construct(SerializerIterator& sit, SField::ref name)
{
	uint64 value = sit.get64();

	if ((value & cNotNative) == 0)
	{ // native
		if ((value & cPosNative) != 0)
			return new STAmount(name, value & ~cPosNative, false); // positive
		return new STAmount(name, value, true); // negative
	}

	uint160 uCurrencyID = sit.get160();
	if (!uCurrencyID)
		throw std::runtime_error("invalid native currency");

	uint160	uIssuerID = sit.get160();

	int offset = static_cast<int>(value >> (64 - 10)); // 10 bits for the offset, sign and "not native" flag
	value &= ~(1023ull << (64-10));

	STAmount*	sapResult;

	if (value)
	{
		bool isNegative = (offset & 256) == 0;
		offset = (offset & 255) - 97; // center the range
		if ((value < cMinValue) || (value > cMaxValue) || (offset < cMinOffset) || (offset > cMaxOffset))
			throw std::runtime_error("invalid currency value");

		sapResult	= new STAmount(name, uCurrencyID, uIssuerID, value, offset, isNegative);
	}
	else
	{
		if (offset != 512)
			throw std::runtime_error("invalid currency value");

		sapResult	= new STAmount(name, uCurrencyID, uIssuerID);
	}

	return sapResult;
}

int64 STAmount::getSNValue() const
{ // signed native value
	if (!mIsNative) throw std::runtime_error("not native");
	if (mIsNegative) return - static_cast<int64>(mValue);
	return static_cast<int64>(mValue);
}

void STAmount::setSNValue(int64 v)
{
	if (!mIsNative) throw std::runtime_error("not native");
	if (v > 0)
	{
		mIsNegative = false;
		mValue = static_cast<uint64>(v);
	}
	else
	{
		mIsNegative = true;
		mValue = static_cast<uint64>(-v);
	}
}

std::string STAmount::getRaw() const
{ // show raw internal form
	if (mValue == 0) return "0";
	if (mIsNative)
	{
		if (mIsNegative) return std::string("-") + boost::lexical_cast<std::string>(mValue);
		else return boost::lexical_cast<std::string>(mValue);
	}
	if (mIsNegative)
		return mCurrency.GetHex() + ": -" +
		boost::lexical_cast<std::string>(mValue) + "e" + boost::lexical_cast<std::string>(mOffset);
	else return mCurrency.GetHex() + ": " +
		boost::lexical_cast<std::string>(mValue) + "e" + boost::lexical_cast<std::string>(mOffset);
}

std::string STAmount::getText() const
{ // keep full internal accuracy, but make more human friendly if posible
	if (isZero()) return "0";
	if (mIsNative)
	{
		if (mIsNegative)
			return std::string("-") +  boost::lexical_cast<std::string>(mValue);
		else return boost::lexical_cast<std::string>(mValue);
	}
	if ((mOffset < -25) || (mOffset > -5))
	{
		if (mIsNegative)
			return std::string("-") + boost::lexical_cast<std::string>(mValue) +
				"e" + boost::lexical_cast<std::string>(mOffset);
		else
			return boost::lexical_cast<std::string>(mValue) + "e" + boost::lexical_cast<std::string>(mOffset);
	}

	std::string val = "000000000000000000000000000";
	val += boost::lexical_cast<std::string>(mValue);
	val += "00000000000000000000000";

	std::string pre = val.substr(0, mOffset + 43);
	std::string post = val.substr(mOffset + 43);

	size_t s_pre = pre.find_first_not_of('0');
	if (s_pre == std::string::npos)
		pre="0";
	else
		pre = pre.substr(s_pre);

	size_t s_post = post.find_last_not_of('0');

	if (mIsNegative) pre = std::string("-") + pre;

	if (s_post == std::string::npos)
		return pre;
	else
		return pre + "." + post.substr(0, s_post+1);
}

bool STAmount::isComparable(const STAmount& t) const
{ // are these two STAmount instances in the same currency
	if (mIsNative) return t.mIsNative;
	if (t.mIsNative) return false;
	return mCurrency == t.mCurrency;
}

bool STAmount::isEquivalent(const SerializedType& t) const
{
	const STAmount* v = dynamic_cast<const STAmount*>(&t);
	if (!v) return false;
	return isComparable(*v) && (mIsNegative == v->mIsNegative) && (mValue == v->mValue) && (mOffset == v->mOffset);
}

void STAmount::throwComparable(const STAmount& t) const
{ // throw an exception if these two STAmount instances are incomparable
	if (!isComparable(t))
		throw std::runtime_error("amounts are not comparable");
}

bool STAmount::operator==(const STAmount& a) const
{
	return isComparable(a) && (mIsNegative == a.mIsNegative) && (mOffset == a.mOffset) && (mValue == a.mValue);
}

bool STAmount::operator!=(const STAmount& a) const
{
	return (mOffset != a.mOffset) || (mValue != a.mValue) || (mIsNegative != a.mIsNegative) || !isComparable(a);
}

bool STAmount::operator<(const STAmount& a) const
{
	throwComparable(a);
	return toUInt64() < a.toUInt64();
}

bool STAmount::operator>(const STAmount& a) const
{
	throwComparable(a);
	return toUInt64() > a.toUInt64();
}

bool STAmount::operator<=(const STAmount& a) const
{
	throwComparable(a);
	return toUInt64() <= a.toUInt64();
}

bool STAmount::operator>=(const STAmount& a) const
{
	throwComparable(a);
	return toUInt64() >= a.toUInt64();
}

STAmount& STAmount::operator+=(const STAmount& a)
{
	*this = *this + a;
	return *this;
}

STAmount& STAmount::operator-=(const STAmount& a)
{
	*this = *this - a;
	return *this;
}

STAmount STAmount::operator-(void) const
{
	if (mValue == 0) return *this;
	return STAmount(getFName(), mCurrency, mIssuer, mValue, mOffset, mIsNative, !mIsNegative);
}

STAmount& STAmount::operator=(uint64 v)
{ // does not copy name, does not change currency type
	mOffset = 0;
	mValue = v;
	mIsNegative = false;
	if (!mIsNative) canonicalize();

	return *this;
}

STAmount& STAmount::operator+=(uint64 v)
{
	if (mIsNative)
		setSNValue(getSNValue() + static_cast<int64>(v));
	else *this += STAmount(mCurrency, v);

	return *this;
}

STAmount& STAmount::operator-=(uint64 v)
{
	if (mIsNative)
		setSNValue(getSNValue() - static_cast<int64>(v));
	else *this -= STAmount(mCurrency, v);

	return *this;
}

bool STAmount::operator<(uint64 v) const
{
	return getSNValue() < static_cast<int64>(v);
}

bool STAmount::operator>(uint64 v) const
{
	return getSNValue() > static_cast<int64>(v);
}

bool STAmount::operator<=(uint64 v) const
{
	return getSNValue() <= static_cast<int64>(v);
}

bool STAmount::operator>=(uint64 v) const
{
	return getSNValue() >= static_cast<int64>(v);
}

STAmount STAmount::operator+(uint64 v) const
{
	return STAmount(getFName(), getSNValue() + static_cast<int64>(v));
}

STAmount STAmount::operator-(uint64 v) const
{
	return STAmount(getFName(), getSNValue() - static_cast<int64>(v));
}

STAmount::operator double() const
{ // Does not keep the precise value. Not recommended
	if (!mValue)
		return 0.0;
	if (mIsNegative) return -1.0 * static_cast<double>(mValue) * pow(10.0, mOffset);
	return static_cast<double>(mValue) * pow(10.0, mOffset);
}

STAmount operator+(const STAmount& v1, const STAmount& v2)
{
	if (v1.isZero()) return v2;
	if (v2.isZero()) return v1;

	v1.throwComparable(v2);
	if (v1.mIsNative)
		return STAmount(v1.getFName(), v1.getSNValue() + v2.getSNValue());


	int ov1 = v1.mOffset, ov2 = v2.mOffset;
	int64 vv1 = static_cast<int64>(v1.mValue), vv2 = static_cast<int64>(v2.mValue);
	if (v1.mIsNegative) vv1 = -vv1;
	if (v2.mIsNegative) vv2 = -vv2;

	while (ov1 < ov2)
	{
		vv1 /= 10;
		++ov1;
	}
	while (ov2 < ov1)
	{
		vv2 /= 10;
		++ov2;
	}
	// this addition cannot overflow an int64, it can overflow an STAmount and the constructor will throw

	int64 fv = vv1 + vv2;
	if (fv >= 0)
		return STAmount(v1.getFName(), v1.mCurrency, v1.mIssuer, fv, ov1, false);
	else
		return STAmount(v1.getFName(), v1.mCurrency, v1.mIssuer, -fv, ov1, true);
}

STAmount operator-(const STAmount& v1, const STAmount& v2)
{
	if (v2.isZero()) return v1;

	v1.throwComparable(v2);
	if (v2.mIsNative)
		return STAmount(v1.fName, v1.getSNValue() - v2.getSNValue());

	int ov1 = v1.mOffset, ov2 = v2.mOffset;
	int64 vv1 = static_cast<int64>(v1.mValue), vv2 = static_cast<int64>(v2.mValue);
	if (v1.mIsNegative) vv1 = -vv1;
	if (v2.mIsNegative) vv2 = -vv2;

	while (ov1 < ov2)
	{
		vv1 /= 10;
		++ov1;
	}
	while (ov2 < ov1)
	{
		vv2 /= 10;
		++ov2;
	}
	// this subtraction cannot overflow an int64, it can overflow an STAmount and the constructor will throw

	int64 fv = vv1 - vv2;
	if (fv >= 0)
		return STAmount(v1.getFName(), v1.mCurrency, v1.mIssuer, fv, ov1, false);
	else
		return STAmount(v1.getFName(), v1.mCurrency, v1.mIssuer, -fv, ov1, true);
}

STAmount STAmount::divide(const STAmount& num, const STAmount& den, const uint160& uCurrencyID, const uint160& uIssuerID)
{
	if (den.isZero()) throw std::runtime_error("division by zero");
	if (num.isZero()) return STAmount(uCurrencyID, uIssuerID);

	uint64 numVal = num.mValue, denVal = den.mValue;
	int numOffset = num.mOffset, denOffset = den.mOffset;

	if (num.mIsNative)
		while (numVal < STAmount::cMinValue)
		{ // Need to bring into range
			numVal *= 10;
			--numOffset;
		}

	if (den.mIsNative)
		while (denVal < STAmount::cMinValue)
		{
			denVal *= 10;
			--denOffset;
		}

	int finOffset = numOffset - denOffset - 16;
	if ((finOffset > cMaxOffset) || (finOffset < cMinOffset))
		throw std::runtime_error("division produces out of range result");

	// Compute (numerator * 10^16) / denominator
	CBigNum v;
	if ((BN_add_word(&v, numVal) != 1) ||
		(BN_mul_word(&v, 10000000000000000ul) != 1) ||
		(BN_div_word(&v, denVal) == ((BN_ULONG) -1)))
	{
		throw std::runtime_error("internal bn error");
	}

	// 10^15 <= quotient <= 10^17
	assert(BN_num_bytes(&v) <= 64);

	if (num.mIsNegative != den.mIsNegative)
		return -STAmount(uCurrencyID, uIssuerID, v.getulong(), finOffset);
	else return STAmount(uCurrencyID, uIssuerID, v.getulong(), finOffset);
}

STAmount STAmount::multiply(const STAmount& v1, const STAmount& v2, const uint160& uCurrencyID, const uint160& uIssuerID)
{
	if (v1.isZero() || v2.isZero())
		return STAmount(uCurrencyID, uIssuerID);

	if (v1.mIsNative && v2.mIsNative) // FIXME: overflow
		return STAmount(v1.getFName(), v1.getSNValue() * v2.getSNValue());

	uint64 value1 = v1.mValue, value2 = v2.mValue;
	int offset1 = v1.mOffset, offset2 = v2.mOffset;


	if (v1.mIsNative)
	{
		while (value1 < STAmount::cMinValue)
		{
			value1 *= 10;
			--offset1;
		}
	}
	else
	{ // round
		value1 *= 10;
		value1 += 3;
		--offset1;
	}

	if (v2.mIsNative)
	{
		while (value2 < STAmount::cMinValue)
		{
			value2 *= 10;
			--offset2;
		}
	}
	else
	{ // round
		value2 *= 10;
		value2 += 3;
		--offset2;
	}

	int finOffset = offset1 + offset2;
	if ((finOffset > 80) || (finOffset < -96))
		throw std::runtime_error("multiplication produces out of range result");

	// Compute (numerator*10 * denominator*10) / 10^18 with rounding
	CBigNum v;
	if ((BN_add_word(&v, value1) != 1) ||
		(BN_mul_word(&v, value2) != 1) ||
		(BN_div_word(&v, 100000000000000ul) == ((BN_ULONG) -1)))
	{
		throw std::runtime_error("internal bn error");
	}

	// 10^16 <= product <= 10^18
	assert(BN_num_bytes(&v) <= 64);

	if (v1.mIsNegative != v2.mIsNegative)
		return -STAmount(uCurrencyID, uIssuerID, v.getulong(), offset1 + offset2 + 14);
	else return STAmount(uCurrencyID, uIssuerID, v.getulong(), offset1 + offset2 + 14);
}

// Convert an offer into an index amount so they sort by rate.
// A taker will take the best, lowest, rate first.
// (e.g. a taker will prefer pay 1 get 3 over pay 1 get 2.
// --> offerOut: takerGets: How much the offerer is selling to the taker.
// -->  offerIn: takerPays: How much the offerer is receiving from the taker.
// <--    uRate: normalize(offerIn/offerOut)
//             A lower rate is better for the person taking the order.
//             The taker gets more for less with a lower rate.
uint64 STAmount::getRate(const STAmount& offerOut, const STAmount& offerIn)
{
	if (offerOut.isZero()) throw std::runtime_error("Worthless offer");

	STAmount r = divide(offerIn, offerOut, CURRENCY_ONE, ACCOUNT_ONE);

	assert((r.getExponent() >= -100) && (r.getExponent() <= 155));

	uint64 ret = r.getExponent() + 100;

	return (ret << (64 - 8)) | r.getMantissa();
}

STAmount STAmount::setRate(uint64 rate)
{
	uint64 mantissa = rate & ~(255ull << (64 - 8));
	int exponent = static_cast<int>(rate >> (64 - 8)) - 100;

	return STAmount(CURRENCY_ONE, ACCOUNT_ONE, mantissa, exponent);
}

// Taker gets all taker can pay for with saTakerFunds, limited by saOfferPays and saOfferFunds.
// -->  saOfferFunds: Limit for saOfferPays
// -->  saTakerFunds: Limit for saOfferGets : How much taker really wants. : Driver
// -->   saOfferPays: Request : this should be reduced as the offer is fullfilled.
// -->   saOfferGets: Request : this should be reduced as the offer is fullfilled.
// -->   saTakerPays: Total : Used to know the approximate ratio of the exchange.
// -->   saTakerGets: Total : Used to know the approximate ratio of the exchange.
// <--   saTakerPaid: Actual
// <--    saTakerGot: Actual
// <--       bRemove: remove offer it is either fullfilled or unfunded
bool STAmount::applyOffer(
	const STAmount& saOfferFunds, const STAmount& saTakerFunds,
	const STAmount& saOfferPays, const STAmount& saOfferGets,
	const STAmount& saTakerPays, const STAmount& saTakerGets,
	STAmount& saTakerPaid, STAmount& saTakerGot)
{
	saOfferGets.throwComparable(saTakerPays);

	assert(!saOfferFunds.isZero() && !saTakerFunds.isZero());	// Must have funds.
	assert(!saOfferGets.isZero() && !saOfferPays.isZero());		// Must not be a null offer.

	// Amount offer can pay out, limited by offer and funds.
	STAmount	saOfferPaysAvailable	= saOfferFunds < saOfferPays ? saOfferFunds : saOfferPays;

	// Amount offer can get in proportion, limited by offer funds.
	STAmount	saOfferGetsAvailable =
		saOfferFunds == saOfferPays
			? saOfferGets	// Offer was fully funded, avoid shenanigans.
			: divide(multiply(saTakerPays, saOfferPaysAvailable, CURRENCY_ONE, ACCOUNT_ONE), saTakerGets, saOfferGets.getCurrency(), saOfferGets.getIssuer());

	if (saOfferGets == saOfferGetsAvailable && saTakerFunds >= saOfferGets)
	{
		// Taker gets all of offer available.
		saTakerPaid	= saOfferGets;		// Taker paid what offer could get.
		saTakerGot	= saOfferPays;		// Taker got what offer could pay.

		Log(lsINFO) << "applyOffer: took all outright";
	}
	else if (saTakerFunds >= saOfferGetsAvailable)
	{
		// Taker gets all of offer available.
		saTakerPaid	= saOfferGetsAvailable;		// Taker paid what offer could get.
		saTakerGot	= saOfferPaysAvailable;		// Taker got what offer could pay.

		Log(lsINFO) << "applyOffer: took all available";
	}
	else
	{
		// Taker only get's a portion of offer.
		saTakerPaid	= saTakerFunds;					// Taker paid all he had.
		saTakerGot	= divide(multiply(saTakerFunds, saOfferPaysAvailable, CURRENCY_ONE, ACCOUNT_ONE), saOfferGetsAvailable, saOfferPays.getCurrency(), saOfferPays.getIssuer());

		Log(lsINFO) << "applyOffer: saTakerGot=" << saTakerGot;
		Log(lsINFO) << "applyOffer: saOfferPaysAvailable=" << saOfferPaysAvailable;
	}

	return saTakerGot >= saOfferPays;
}

STAmount STAmount::getPay(const STAmount& offerOut, const STAmount& offerIn, const STAmount& needed)
{ // Someone wants to get (needed) out of the offer, how much should they pay in?
	if (offerOut.isZero())
		return STAmount(offerIn.getCurrency(), offerIn.getIssuer());

	if (needed >= offerOut)
	{
		// They need more than offered, pay full amount.
		return needed;
	}

	STAmount ret = divide(multiply(needed, offerIn, CURRENCY_ONE, ACCOUNT_ONE), offerOut, offerIn.getCurrency(), offerIn.getIssuer());

	return (ret > offerIn) ? offerIn : ret;
}

uint64 STAmount::muldiv(uint64 a, uint64 b, uint64 c)
{ // computes (a*b)/c rounding up - supports values up to 10^18
	if (c == 0) throw std::runtime_error("underflow");
	if ((a == 0) || (b == 0)) return 0;

	CBigNum v;
	if ((BN_add_word(&v, a * 10 + 3) != 1) || (BN_mul_word(&v, b * 10 + 3) != 1) ||
		(BN_div_word(&v, c) == ((BN_ULONG) -1)) || (BN_div_word(&v, 100) == ((BN_ULONG) -1)))
		throw std::runtime_error("muldiv error");

	return v.getulong();
}

uint64 STAmount::convertToDisplayAmount(const STAmount& internalAmount, uint64 totalNow, uint64 totalInit)
{ // Convert an internal ledger/account quantity of native currency to a display amount
	return muldiv(internalAmount.getNValue(), totalInit, totalNow);
}

STAmount STAmount::convertToInternalAmount(uint64 displayAmount, uint64 totalNow, uint64 totalInit, SField::ref name)
{ // Convert a display/request currency amount to an internal amount
	return STAmount(name, muldiv(displayAmount, totalNow, totalInit));
}

STAmount STAmount::deserialize(SerializerIterator& it)
{
	STAmount *s = construct(it, sfGeneric);
	STAmount ret(*s);

	delete s;

	return ret;
}

std::string STAmount::getFullText() const
{
	if (mIsNative)
	{
		return str(boost::format("%s/" SYSTEM_CURRENCY_CODE) % getText());
	}
	else if (!mIssuer)
	{
		return str(boost::format("%s/%s/0") % getText() % getHumanCurrency());
	}
	else if (mIssuer == ACCOUNT_ONE)
	{
		return str(boost::format("%s/%s/1") % getText() % getHumanCurrency());
	}
	else
	{
		return str(boost::format("%s/%s/%s")
			% getText()
			% getHumanCurrency()
			% NewcoinAddress::createHumanAccountID(mIssuer));
	}
}

#if 0
std::string STAmount::getExtendedText() const
{
	if (mIsNative)
	{
		return str(boost::format("%s " SYSTEM_CURRENCY_CODE) % getText());
	}
	else
	{
		return str(boost::format("%s/%s/%s %dE%d" )
			% getText()
			% getHumanCurrency()
			% NewcoinAddress::createHumanAccountID(mIssuer)
			% getMantissa()
			% getExponent());
	}
}
#endif

Json::Value STAmount::getJson(int) const
{
	Json::Value elem(Json::objectValue);

	// This is a hack, many places don't specify a currency.  STAmount is used just as a value.
	if (!mIsNative)
	{
		elem["value"]		= getText();
		elem["currency"]	= getHumanCurrency();

		if (!mIssuer.isZero())
			elem["issuer"]	= NewcoinAddress::createHumanAccountID(mIssuer);

	}
	else
	{
		elem=getText();
	}

	return elem;
}

// For unit tests:
static STAmount serdes(const STAmount &s)
{
	Serializer ser;

	s.add(ser);

	SerializerIterator sit(ser);

	return STAmount::deserialize(sit);
}

BOOST_AUTO_TEST_SUITE(amount)

BOOST_AUTO_TEST_CASE( setValue_test )
{
	STAmount	saTmp;

	// Check native floats
	saTmp.setFullValue("1^0"); BOOST_CHECK_MESSAGE(SYSTEM_CURRENCY_PARTS == saTmp.getNValue(), "float integer failed");
	saTmp.setFullValue("0^1"); BOOST_CHECK_MESSAGE(SYSTEM_CURRENCY_PARTS/10 == saTmp.getNValue(), "float fraction failed");
	saTmp.setFullValue("0^12"); BOOST_CHECK_MESSAGE(12*SYSTEM_CURRENCY_PARTS/100 == saTmp.getNValue(), "float fraction failed");
	saTmp.setFullValue("1^2"); BOOST_CHECK_MESSAGE(SYSTEM_CURRENCY_PARTS+(2*SYSTEM_CURRENCY_PARTS/10) == saTmp.getNValue(), "float combined failed");

	// Check native integer
	saTmp.setFullValue("1"); BOOST_CHECK_MESSAGE(1 == saTmp.getNValue(), "integer failed");
}

BOOST_AUTO_TEST_CASE( NativeCurrency_test )
{
	STAmount zero, one(1), hundred(100);

	if (sizeof(BN_ULONG) < (64 / 8)) BOOST_FAIL("BN too small");
	if (serdes(zero) != zero) BOOST_FAIL("STAmount fail");
	if (serdes(one) != one) BOOST_FAIL("STAmount fail");
	if (serdes(hundred) != hundred) BOOST_FAIL("STAmount fail");

	if (!zero.isNative()) BOOST_FAIL("STAmount fail");
	if (!hundred.isNative()) BOOST_FAIL("STAmount fail");
	if (!zero.isZero()) BOOST_FAIL("STAmount fail");
	if (one.isZero()) BOOST_FAIL("STAmount fail");
	if (hundred.isZero()) BOOST_FAIL("STAmount fail");
	if ((zero < zero)) BOOST_FAIL("STAmount fail");
	if (!(zero < one)) BOOST_FAIL("STAmount fail");
	if (!(zero < hundred)) BOOST_FAIL("STAmount fail");
	if ((one < zero)) BOOST_FAIL("STAmount fail");
	if ((one < one)) BOOST_FAIL("STAmount fail");
	if (!(one < hundred)) BOOST_FAIL("STAmount fail");
	if ((hundred < zero)) BOOST_FAIL("STAmount fail");
	if ((hundred < one)) BOOST_FAIL("STAmount fail");
	if ((hundred < hundred)) BOOST_FAIL("STAmount fail");
	if ((zero > zero)) BOOST_FAIL("STAmount fail");
	if ((zero > one)) BOOST_FAIL("STAmount fail");
	if ((zero > hundred)) BOOST_FAIL("STAmount fail");
	if (!(one > zero)) BOOST_FAIL("STAmount fail");
	if ((one > one)) BOOST_FAIL("STAmount fail");
	if ((one > hundred)) BOOST_FAIL("STAmount fail");
	if (!(hundred > zero)) BOOST_FAIL("STAmount fail");
	if (!(hundred > one)) BOOST_FAIL("STAmount fail");
	if ((hundred > hundred)) BOOST_FAIL("STAmount fail");
	if (!(zero <= zero)) BOOST_FAIL("STAmount fail");
	if (!(zero <= one)) BOOST_FAIL("STAmount fail");
	if (!(zero <= hundred)) BOOST_FAIL("STAmount fail");
	if ((one <= zero)) BOOST_FAIL("STAmount fail");
	if (!(one <= one)) BOOST_FAIL("STAmount fail");
	if (!(one <= hundred)) BOOST_FAIL("STAmount fail");
	if ((hundred <= zero)) BOOST_FAIL("STAmount fail");
	if ((hundred <= one)) BOOST_FAIL("STAmount fail");
	if (!(hundred <= hundred)) BOOST_FAIL("STAmount fail");
	if (!(zero >= zero)) BOOST_FAIL("STAmount fail");
	if ((zero >= one)) BOOST_FAIL("STAmount fail");
	if ((zero >= hundred)) BOOST_FAIL("STAmount fail");
	if (!(one >= zero)) BOOST_FAIL("STAmount fail");
	if (!(one >= one)) BOOST_FAIL("STAmount fail");
	if ((one >= hundred)) BOOST_FAIL("STAmount fail");
	if (!(hundred >= zero)) BOOST_FAIL("STAmount fail");
	if (!(hundred >= one)) BOOST_FAIL("STAmount fail");
	if (!(hundred >= hundred)) BOOST_FAIL("STAmount fail");
	if (!(zero == zero)) BOOST_FAIL("STAmount fail");
	if ((zero == one)) BOOST_FAIL("STAmount fail");
	if ((zero == hundred)) BOOST_FAIL("STAmount fail");
	if ((one == zero)) BOOST_FAIL("STAmount fail");
	if (!(one == one)) BOOST_FAIL("STAmount fail");
	if ((one == hundred)) BOOST_FAIL("STAmount fail");
	if ((hundred == zero)) BOOST_FAIL("STAmount fail");
	if ((hundred == one)) BOOST_FAIL("STAmount fail");
	if (!(hundred == hundred)) BOOST_FAIL("STAmount fail");
	if ((zero != zero)) BOOST_FAIL("STAmount fail");
	if (!(zero != one)) BOOST_FAIL("STAmount fail");
	if (!(zero != hundred)) BOOST_FAIL("STAmount fail");
	if (!(one != zero)) BOOST_FAIL("STAmount fail");
	if ((one != one)) BOOST_FAIL("STAmount fail");
	if (!(one != hundred)) BOOST_FAIL("STAmount fail");
	if (!(hundred != zero)) BOOST_FAIL("STAmount fail");
	if (!(hundred != one)) BOOST_FAIL("STAmount fail");
	if ((hundred != hundred)) BOOST_FAIL("STAmount fail");
	if (STAmount().getText() != "0") BOOST_FAIL("STAmount fail");
	if (STAmount(31).getText() != "31")	BOOST_FAIL("STAmount fail");
	if (STAmount(310).getText() != "310") BOOST_FAIL("STAmount fail");
	BOOST_TEST_MESSAGE("Amount NC Complete");
}

BOOST_AUTO_TEST_CASE( CustomCurrency_test )
{
	STAmount zero(CURRENCY_ONE, ACCOUNT_ONE), one(CURRENCY_ONE, ACCOUNT_ONE, 1), hundred(CURRENCY_ONE, ACCOUNT_ONE, 100);

	serdes(one).getRaw();

	if (serdes(zero) != zero) BOOST_FAIL("STAmount fail");
	if (serdes(one) != one) BOOST_FAIL("STAmount fail");
	if (serdes(hundred) != hundred) BOOST_FAIL("STAmount fail");

	if (zero.isNative()) BOOST_FAIL("STAmount fail");
	if (hundred.isNative()) BOOST_FAIL("STAmount fail");
	if (!zero.isZero()) BOOST_FAIL("STAmount fail");
	if (one.isZero()) BOOST_FAIL("STAmount fail");
	if (hundred.isZero()) BOOST_FAIL("STAmount fail");
	if ((zero < zero)) BOOST_FAIL("STAmount fail");
	if (!(zero < one)) BOOST_FAIL("STAmount fail");
	if (!(zero < hundred)) BOOST_FAIL("STAmount fail");
	if ((one < zero)) BOOST_FAIL("STAmount fail");
	if ((one < one)) BOOST_FAIL("STAmount fail");
	if (!(one < hundred)) BOOST_FAIL("STAmount fail");
	if ((hundred < zero)) BOOST_FAIL("STAmount fail");
	if ((hundred < one)) BOOST_FAIL("STAmount fail");
	if ((hundred < hundred)) BOOST_FAIL("STAmount fail");
	if ((zero > zero)) BOOST_FAIL("STAmount fail");
	if ((zero > one)) BOOST_FAIL("STAmount fail");
	if ((zero > hundred)) BOOST_FAIL("STAmount fail");
	if (!(one > zero)) BOOST_FAIL("STAmount fail");
	if ((one > one)) BOOST_FAIL("STAmount fail");
	if ((one > hundred)) BOOST_FAIL("STAmount fail");
	if (!(hundred > zero)) BOOST_FAIL("STAmount fail");
	if (!(hundred > one)) BOOST_FAIL("STAmount fail");
	if ((hundred > hundred)) BOOST_FAIL("STAmount fail");
	if (!(zero <= zero)) BOOST_FAIL("STAmount fail");
	if (!(zero <= one)) BOOST_FAIL("STAmount fail");
	if (!(zero <= hundred)) BOOST_FAIL("STAmount fail");
	if ((one <= zero)) BOOST_FAIL("STAmount fail");
	if (!(one <= one)) BOOST_FAIL("STAmount fail");
	if (!(one <= hundred)) BOOST_FAIL("STAmount fail");
	if ((hundred <= zero)) BOOST_FAIL("STAmount fail");
	if ((hundred <= one)) BOOST_FAIL("STAmount fail");
	if (!(hundred <= hundred)) BOOST_FAIL("STAmount fail");
	if (!(zero >= zero)) BOOST_FAIL("STAmount fail");
	if ((zero >= one)) BOOST_FAIL("STAmount fail");
	if ((zero >= hundred)) BOOST_FAIL("STAmount fail");
	if (!(one >= zero)) BOOST_FAIL("STAmount fail");
	if (!(one >= one)) BOOST_FAIL("STAmount fail");
	if ((one >= hundred)) BOOST_FAIL("STAmount fail");
	if (!(hundred >= zero)) BOOST_FAIL("STAmount fail");
	if (!(hundred >= one)) BOOST_FAIL("STAmount fail");
	if (!(hundred >= hundred)) BOOST_FAIL("STAmount fail");
	if (!(zero == zero)) BOOST_FAIL("STAmount fail");
	if ((zero == one)) BOOST_FAIL("STAmount fail");
	if ((zero == hundred)) BOOST_FAIL("STAmount fail");
	if ((one == zero)) BOOST_FAIL("STAmount fail");
	if (!(one == one)) BOOST_FAIL("STAmount fail");
	if ((one == hundred)) BOOST_FAIL("STAmount fail");
	if ((hundred == zero)) BOOST_FAIL("STAmount fail");
	if ((hundred == one)) BOOST_FAIL("STAmount fail");
	if (!(hundred == hundred)) BOOST_FAIL("STAmount fail");
	if ((zero != zero)) BOOST_FAIL("STAmount fail");
	if (!(zero != one)) BOOST_FAIL("STAmount fail");
	if (!(zero != hundred)) BOOST_FAIL("STAmount fail");
	if (!(one != zero)) BOOST_FAIL("STAmount fail");
	if ((one != one)) BOOST_FAIL("STAmount fail");
	if (!(one != hundred)) BOOST_FAIL("STAmount fail");
	if (!(hundred != zero)) BOOST_FAIL("STAmount fail");
	if (!(hundred != one)) BOOST_FAIL("STAmount fail");
	if ((hundred != hundred)) BOOST_FAIL("STAmount fail");
	if (STAmount(CURRENCY_ONE, ACCOUNT_ONE).getText() != "0") BOOST_FAIL("STAmount fail");
	if (STAmount(CURRENCY_ONE, ACCOUNT_ONE, 31).getText() != "31") BOOST_FAIL("STAmount fail");
	if (STAmount(CURRENCY_ONE, ACCOUNT_ONE, 31,1).getText() != "310") BOOST_FAIL("STAmount fail");
	if (STAmount(CURRENCY_ONE, ACCOUNT_ONE, 31,-1).getText() != "3.1") BOOST_FAIL("STAmount fail");
	if (STAmount(CURRENCY_ONE, ACCOUNT_ONE, 31,-2).getText() != "0.31") BOOST_FAIL("STAmount fail");

	if (STAmount::multiply(STAmount(CURRENCY_ONE, ACCOUNT_ONE, 20), STAmount(3), CURRENCY_ONE, ACCOUNT_ONE).getText() != "60")
		BOOST_FAIL("STAmount multiply fail");
	if (STAmount::multiply(STAmount(CURRENCY_ONE, ACCOUNT_ONE, 20), STAmount(3), uint160(), ACCOUNT_XNS).getText() != "60")
		BOOST_FAIL("STAmount multiply fail");
	if (STAmount::multiply(STAmount(20), STAmount(3), CURRENCY_ONE, ACCOUNT_ONE).getText() != "60")
		BOOST_FAIL("STAmount multiply fail");
	if (STAmount::multiply(STAmount(20), STAmount(3), uint160(), ACCOUNT_XNS).getText() != "60")
		BOOST_FAIL("STAmount multiply fail");
	if (STAmount::divide(STAmount(CURRENCY_ONE, ACCOUNT_ONE, 60), STAmount(3), CURRENCY_ONE, ACCOUNT_ONE).getText() != "20")
		BOOST_FAIL("STAmount divide fail");
	if (STAmount::divide(STAmount(CURRENCY_ONE, ACCOUNT_ONE, 60), STAmount(3), uint160(), ACCOUNT_XNS).getText() != "20")
		BOOST_FAIL("STAmount divide fail");
	if (STAmount::divide(STAmount(CURRENCY_ONE, ACCOUNT_ONE, 60), STAmount(CURRENCY_ONE, ACCOUNT_ONE, 3), CURRENCY_ONE, ACCOUNT_ONE).getText() != "20")
		BOOST_FAIL("STAmount divide fail");
	if (STAmount::divide(STAmount(CURRENCY_ONE, ACCOUNT_ONE, 60), STAmount(CURRENCY_ONE, ACCOUNT_ONE, 3), uint160(), ACCOUNT_XNS).getText() != "20")
		BOOST_FAIL("STAmount divide fail");

	STAmount a1(CURRENCY_ONE, ACCOUNT_ONE, 60), a2 (CURRENCY_ONE, ACCOUNT_ONE, 10, -1);
	if (STAmount::divide(a2, a1, CURRENCY_ONE, ACCOUNT_ONE) != STAmount::setRate(STAmount::getRate(a1, a2)))
		BOOST_FAIL("STAmount setRate(getRate) fail");
	if (STAmount::divide(a1, a2, CURRENCY_ONE, ACCOUNT_ONE) != STAmount::setRate(STAmount::getRate(a2, a1)))
		BOOST_FAIL("STAmount setRate(getRate) fail");

	BOOST_TEST_MESSAGE("Amount CC Complete");
}

BOOST_AUTO_TEST_CASE( CurrencyMulDivTests )
{
	// Test currency multiplication and division operations such as
	// convertToDisplayAmount, convertToInternalAmount, getRate, getClaimed, and getNeeded

	if (STAmount::getRate(STAmount(1), STAmount(10)) != (((100ul-14)<<(64-8))|1000000000000000ul))
		BOOST_FAIL("STAmount getRate fail");
	if (STAmount::getRate(STAmount(10), STAmount(1)) != (((100ul-16)<<(64-8))|1000000000000000ul))
		BOOST_FAIL("STAmount getRate fail");
	if (STAmount::getRate(STAmount(CURRENCY_ONE, ACCOUNT_ONE, 1), STAmount(CURRENCY_ONE, ACCOUNT_ONE, 10)) != (((100ul-14)<<(64-8))|1000000000000000ul))
		BOOST_FAIL("STAmount getRate fail");
	if (STAmount::getRate(STAmount(CURRENCY_ONE, ACCOUNT_ONE, 10), STAmount(CURRENCY_ONE, ACCOUNT_ONE, 1)) != (((100ul-16)<<(64-8))|1000000000000000ul))
		BOOST_FAIL("STAmount getRate fail");
	if (STAmount::getRate(STAmount(CURRENCY_ONE, ACCOUNT_ONE, 1), STAmount(10)) != (((100ul-14)<<(64-8))|1000000000000000ul))
		BOOST_FAIL("STAmount getRate fail");
	if (STAmount::getRate(STAmount(CURRENCY_ONE, ACCOUNT_ONE, 10), STAmount(1)) != (((100ul-16)<<(64-8))|1000000000000000ul))
		BOOST_FAIL("STAmount getRate fail");
	if (STAmount::getRate(STAmount(1), STAmount(CURRENCY_ONE, ACCOUNT_ONE, 10)) != (((100ul-14)<<(64-8))|1000000000000000ul))
		BOOST_FAIL("STAmount getRate fail");
	if (STAmount::getRate(STAmount(10), STAmount(CURRENCY_ONE, ACCOUNT_ONE, 1)) != (((100ul-16)<<(64-8))|1000000000000000ul))
		BOOST_FAIL("STAmount getRate fail");

}

BOOST_AUTO_TEST_SUITE_END()

// vim:ts=4
