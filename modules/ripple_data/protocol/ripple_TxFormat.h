#ifndef RIPPLE_TXFORMAT_H
#define RIPPLE_TXFORMAT_H

// VFALCO TODO Rename to TxType
//             Be aware there are some strings "TransactionType"
//             And also we have TransactionType in ripple_SerializeDeclarations.h
//
/** Transaction type identifiers.

    These are part of the binary message format.

    @ingroup protocol
*/
enum TransactionType
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

class TxFormat
{
public:
    TxFormat (char const* name, TransactionType type)
        : m_name (name)
        , m_type (type)
    {
    }

    TxFormat& operator<< (SOElement const& el)
    {
        elements.push_back (el);

        return *this;
    }

    /** Retrieve the name of the format.
    */
    std::string const& getName () const { return m_name; }

    /** Retrieve the transaction type this format represents.
    */
    TransactionType getType () const { return m_type; }

public:
    // VFALCO TODO make an accessor for this
    SOTemplate elements;

private:
    std::string const m_name;
    TransactionType const m_type;
};

#endif
// vim:ts=4
