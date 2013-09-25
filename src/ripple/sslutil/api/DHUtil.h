//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SSLUTIL_DHUTIL_H_INCLUDED
#define RIPPLE_SSLUTIL_DHUTIL_H_INCLUDED

namespace ripple {

DH* DH_der_load (const std::string& strDer);
std::string DH_der_gen (int iKeyLength);

}

#endif
