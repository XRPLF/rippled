//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

namespace ripple {

std::string DH_der_gen (int iKeyLength)
{
    DH*         dh  = 0;
    int         iCodes;
    std::string strDer;

    do
    {
        dh  = DH_generate_parameters (iKeyLength, DH_GENERATOR_5, nullptr, nullptr);
        iCodes  = 0;
        DH_check (dh, &iCodes);
    }
    while (iCodes & (DH_CHECK_P_NOT_PRIME | DH_CHECK_P_NOT_SAFE_PRIME | DH_UNABLE_TO_CHECK_GENERATOR | DH_NOT_SUITABLE_GENERATOR));

    strDer.resize (i2d_DHparams (dh, nullptr));

    unsigned char* next = reinterpret_cast<unsigned char*> (&strDer[0]);

    (void) i2d_DHparams (dh, &next);

    return strDer;
}

DH* DH_der_load (const std::string& strDer)
{
    const unsigned char* pbuf   = reinterpret_cast<const unsigned char*> (&strDer[0]);

    return d2i_DHparams (nullptr, &pbuf, strDer.size ());
}

}
