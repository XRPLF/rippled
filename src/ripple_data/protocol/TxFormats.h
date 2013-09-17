//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TXFORMATS_H_INCLUDED
#define RIPPLE_TXFORMATS_H_INCLUDED

/** Transaction type identifiers.

    These are part of the binary message format.

    @ingroup protocol
*/
enum TxType
{
    ttINVALID           = -1,

    ttPAYMENT           = 0,
    ttCLAIM             = 1, // open
    ttWALLET_ADD        = 2,
    ttACCOUNT_SET       = 3,
    ttPASSWORD_FUND     = 4, // open
    ttREGULAR_KEY_SET   = 5,
    ttNICKNAME_SET      = 6, // open
    ttOFFER_CREATE      = 7,
    ttOFFER_CANCEL      = 8,
    ttCONTRACT          = 9,
    ttCONTRACT_REMOVE   = 10,  // can we use the same msg as offer cancel

    ttTRUST_SET         = 20,

    ttFEATURE           = 100,
    ttFEE               = 101,
};

/** Manages the list of known transaction formats.
*/
class TxFormats : public KnownFormats <TxType>
{
private:
    void addCommonFields (Item& item);

public:
    /** Create the object.
        This will load the object will all the known transaction formats.
    */
    TxFormats ();

    static TxFormats* getInstance ();
};

#endif
