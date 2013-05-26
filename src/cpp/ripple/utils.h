#ifndef __UTILS__
#define __UTILS__

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/version.hpp>

#if BOOST_VERSION < 104700
#error Boost 1.47 or later is required
#endif

#include <openssl/dh.h>

#define nothing()			do {} while (0)
#define fallthru()			do {} while (0)
#define NUMBER(x)			(sizeof(x)/sizeof((x)[0]))
#define ADDRESS(p)			strHex(uint64( ((char*) p) - ((char*) 0)))
#define ADDRESS_SHARED(p)	strHex(uint64( ((char*) (p).get()) - ((char*) 0)))

#define isSetBit(x,y)		(!!((x) & (y)))

boost::posix_time::ptime ptEpoch();
int iToSeconds(boost::posix_time::ptime ptWhen);
boost::posix_time::ptime ptFromSeconds(int iSeconds);
uint64_t utFromSeconds(int iSeconds);

DH* DH_der_load(const std::string& strDer);
std::string DH_der_gen(int iKeyLength);

void getRand(unsigned char *buf, int num);
inline static void getRand(char *buf, int num)		{ return getRand(reinterpret_cast<unsigned char *>(buf), num); }
inline static void getRand(void *buf, int num)		{ return getRand(reinterpret_cast<unsigned char *>(buf), num); }

template<typename T> T range_check(const T& value, const T& minimum, const T& maximum)
{
	if ((value < minimum) || (value > maximum))
		throw std::runtime_error("Value out of range");
	return value;
}

template<typename T> T range_check_min(const T& value, const T& minimum)
{
	if (value < minimum)
		throw std::runtime_error("Value out of range");
	return value;
}

template<typename T> T range_check_max(const T& value, const T& maximum)
{
	if (value > maximum)
		throw std::runtime_error("Value out of range");
	return value;
}

template<typename T, typename U> T range_check_cast(const U& value, const T& minimum, const T& maximum)
{
	if ((value < minimum) || (value > maximum))
		throw std::runtime_error("Value out of range");
	return static_cast<T>(value);
}

extern void NameThread(const char *);

extern bool HaveSustain();
extern std::string StopSustain();
extern std::string DoSustain();

#if (!defined(FORCE_NO_C11X) && (__cplusplus > 201100L)) || defined(FORCE_C11X)

#define C11X
#include			 	<functional>
#define UPTR_T			std::unique_ptr
#define MOVE_P(p)		std::move(p)
#define BIND_TYPE		std::bind
#define FUNCTION_TYPE	std::function
#define P_1				std::placeholders::_1
#define P_2				std::placeholders::_2
#define P_3				std::placeholders::_3
#define P_4				std::placeholders::_4

#else

#include 				<boost/bind.hpp>
#include				<boost/function.hpp>
#define UPTR_T			std::auto_ptr
#define MOVE_P(p)		(p)
#define BIND_TYPE		boost::bind
#define FUNCTION_TYPE	boost::function
#define P_1				_1
#define P_2				_2
#define P_3				_3
#define P_4				_4

#endif

#endif

// vim:ts=4
