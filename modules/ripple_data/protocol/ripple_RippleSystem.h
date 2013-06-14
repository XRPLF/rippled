#ifndef RIPPLE_RIPPLESYSTEM_H
#define RIPPLE_RIPPLESYSTEM_H

/** Protocol specific constant globals.
*/
// VFALCO NOTE use these from now on instead of the macros!!
class RippleSystem
{
public:
    static inline char const* getSystemName ()
    {
        return "ripple";
    }

    static char const* getCurrencyCode ()
    {
        return "XRP";
    }

    static char const* getCurrencyCodeRipple ()
    {
        return "XRR";
    }

    static int getCurrencyPrecision ()
    {
        return 6;
    }
};

// VFALCO TODO I would love to replace these macros with the language
//         constructs above. The problem is the way they are used at
//         the point of call, i.e. "User-agent:" SYSTEM_NAME
//         It will be necessary to rewrite some of them to use string streams.
//
#define SYSTEM_NAME                 "ripple"
#define SYSTEM_CURRENCY_CODE        "XRP"
#define SYSTEM_CURRENCY_PRECISION   6
#define SYSTEM_CURRENCY_CODE_RIPPLE "XRR"

#endif
