//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_RFC1751_H
#define RIPPLE_RFC1751_H

class RFC1751
{
public:
    static int getKeyFromEnglish (std::string& strKey, const std::string& strHuman);

    static void getEnglishFromKey (std::string& strHuman, const std::string& strKey);

    /** Chooses a single dictionary word from the data.

        This is not particularly secure but it can be useful to provide
        a unique name for something given a GUID or fixed data. We use
        it to turn the pubkey_node into an easily remembered and identified
        4 character string.
    */
    static String getWordFromBlob (void const* data, size_t bytes);

private:
    static unsigned long extract (char* s, int start, int length);
    static void btoe (std::string& strHuman, const std::string& strData);
    static void insert (char* s, int x, int start, int length);
    static void standard (std::string& strWord);
    static int wsrch (const std::string& strWord, int iMin, int iMax);
    static int etob (std::string& strData, std::vector<std::string> vsHuman);

    static char const* s_dictionary [];
};

#endif
