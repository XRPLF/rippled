#ifndef XRPL_CRYPTO_RFC1751_H_INCLUDED
#define XRPL_CRYPTO_RFC1751_H_INCLUDED

#include <string>
#include <vector>

namespace ripple {

class RFC1751
{
public:
    static int
    getKeyFromEnglish(std::string& strKey, std::string const& strHuman);

    static void
    getEnglishFromKey(std::string& strHuman, std::string const& strKey);

    /** Chooses a single dictionary word from the data.

        This is not particularly secure but it can be useful to provide
        a unique name for something given a GUID or fixed data. We use
        it to turn the pubkey_node into an easily remembered and identified
        4 character string.
    */
    static std::string
    getWordFromBlob(void const* blob, size_t bytes);

private:
    static unsigned long
    extract(char const* s, int start, int length);
    static void
    btoe(std::string& strHuman, std::string const& strData);
    static void
    insert(char* s, int x, int start, int length);
    static void
    standard(std::string& strWord);
    static int
    wsrch(std::string const& strWord, int iMin, int iMax);
    static int
    etob(std::string& strData, std::vector<std::string> vsHuman);

    static char const* s_dictionary[];
};

}  // namespace ripple

#endif
