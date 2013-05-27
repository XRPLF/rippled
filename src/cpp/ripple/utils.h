#ifndef __UTILS__
#define __UTILS__

#include <openssl/dh.h>

#define nothing()			do {} while (0)
#define fallthru()			do {} while (0)
#define NUMBER(x)			(sizeof(x)/sizeof((x)[0]))
#define ADDRESS(p)			strHex(uint64( ((char*) p) - ((char*) 0)))
#define ADDRESS_SHARED(p)	strHex(uint64( ((char*) (p).get()) - ((char*) 0)))

#define isSetBit(x,y)		(!!((x) & (y)))

DH* DH_der_load(const std::string& strDer);
std::string DH_der_gen(int iKeyLength);

#endif

// vim:ts=4
