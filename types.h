#ifndef __TYPES_HH__
#define __TYPES_HH__

#if defined(_MSC_VER) || defined(__BORLANDC__)

typedef __int64  int64;
typedef unsigned __int64  uint64;
typedef unsigned int uint32;
typedef unsigned short int uint16;
typedef int int32;

#else

typedef long long  int64;
typedef unsigned long long  uint64;
typedef unsigned int uint32;
typedef unsigned short int uint16;
typedef int int32;

#endif
#if defined(_MSC_VER) && _MSC_VER < 1300
#define for  if (false) ; else for
#endif

#endif
