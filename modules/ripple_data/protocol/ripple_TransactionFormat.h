#ifndef RIPPLE_TRANSACTIONFORMAT_H
#define RIPPLE_TRANSACTIONFORMAT_H

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

class TransactionFormat
{
public:
    std::string                 t_name;
    TransactionType             t_type;
    SOTemplate                  elements;

    static std::map<int, TransactionFormat*>            byType;
    static std::map<std::string, TransactionFormat*>    byName;

    TransactionFormat (const char* name, TransactionType type) : t_name (name), t_type (type)
    {
        byName[name] = this;
        byType[type] = this;
    }
    TransactionFormat& operator<< (const SOElement& el)
    {
        elements.push_back (el);
        return *this;
    }

    static TransactionFormat* getTxnFormat (TransactionType t);
    static TransactionFormat* getTxnFormat (const std::string& t);
    static TransactionFormat* getTxnFormat (int t);
};

const int TransactionMinLen         = 32;
const int TransactionMaxLen         = 1048576;

//
// Transaction flags.
//

// AccountSet flags:
const uint32 tfRequireDestTag       = 0x00010000;
const uint32 tfOptionalDestTag      = 0x00020000;
const uint32 tfRequireAuth          = 0x00040000;
const uint32 tfOptionalAuth         = 0x00080000;
const uint32 tfDisallowXRP          = 0x00100000;
const uint32 tfAllowXRP             = 0x00200000;
const uint32 tfAccountSetMask       = ~ (tfRequireDestTag | tfOptionalDestTag
                                      | tfRequireAuth | tfOptionalAuth
                                      | tfDisallowXRP | tfAllowXRP);

// OfferCreate flags:
const uint32 tfPassive              = 0x00010000;
const uint32 tfImmediateOrCancel    = 0x00020000;
const uint32 tfFillOrKill           = 0x00040000;
const uint32 tfSell                 = 0x00080000;
const uint32 tfOfferCreateMask      = ~ (tfPassive | tfImmediateOrCancel | tfFillOrKill | tfSell);

// Payment flags:
const uint32 tfNoRippleDirect       = 0x00010000;
const uint32 tfPartialPayment       = 0x00020000;
const uint32 tfLimitQuality         = 0x00040000;
const uint32 tfPaymentMask          = ~ (tfPartialPayment | tfLimitQuality | tfNoRippleDirect);

// TrustSet flags:
const uint32 tfSetfAuth             = 0x00010000;
const uint32 tfTrustSetMask         = ~ (tfSetfAuth);

#endif
// vim:ts=4
