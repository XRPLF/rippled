#ifndef RIPPLE_RFC1751_H
#define RIPPLE_RFC1751_H

class RFC1751
{
public:
    static int getKeyFromEnglish (std::string& strKey, const std::string& strHuman);

    static void getEnglishFromKey (std::string& strHuman, const std::string& strKey);

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
