//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
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
