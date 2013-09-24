//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

LocalisedStrings::LocalisedStrings (const String& fileContents, bool ignoreCase)
{
    loadFromText (fileContents, ignoreCase);
}

LocalisedStrings::LocalisedStrings (const File& fileToLoad, bool ignoreCase)
{
    loadFromText (fileToLoad.loadFileAsString(), ignoreCase);
}

LocalisedStrings::~LocalisedStrings()
{
}

//==============================================================================
String LocalisedStrings::translate (const String& text) const
{
    return translations.getValue (text, text);
}

String LocalisedStrings::translate (const String& text, const String& resultIfNotFound) const
{
    return translations.getValue (text, resultIfNotFound);
}

namespace
{
   #if BEAST_CHECK_MEMORY_LEAKS
    // By using this object to force a LocalisedStrings object to be created
    // before the currentMappings object, we can force the static order-of-destruction to
    // delete the currentMappings object first, which avoids a bogus leak warning.
    // (Oddly, just creating a LocalisedStrings on the stack doesn't work in gcc, it
    // has to be created with 'new' for this to work..)
    struct LeakAvoidanceTrick
    {
        LeakAvoidanceTrick()
        {
            const ScopedPointer<LocalisedStrings> dummy (new LocalisedStrings (String(), false));
        }
    };

    LeakAvoidanceTrick leakAvoidanceTrick;
   #endif

    SpinLock currentMappingsLock;
    ScopedPointer<LocalisedStrings> currentMappings;

    int findCloseQuote (const String& text, int startPos)
    {
        beast_wchar lastChar = 0;
        String::CharPointerType t (text.getCharPointer() + startPos);

        for (;;)
        {
            const beast_wchar c = t.getAndAdvance();

            if (c == 0 || (c == '"' && lastChar != '\\'))
                break;

            lastChar = c;
            ++startPos;
        }

        return startPos;
    }

    String unescapeString (const String& s)
    {
        return s.replace ("\\\"", "\"")
                .replace ("\\\'", "\'")
                .replace ("\\t", "\t")
                .replace ("\\r", "\r")
                .replace ("\\n", "\n");
    }
}

void LocalisedStrings::loadFromText (const String& fileContents, bool ignoreCase)
{
    translations.setIgnoresCase (ignoreCase);

    StringArray lines;
    lines.addLines (fileContents);

    for (int i = 0; i < lines.size(); ++i)
    {
        String line (lines[i].trim());

        if (line.startsWithChar ('"'))
        {
            int closeQuote = findCloseQuote (line, 1);

            const String originalText (unescapeString (line.substring (1, closeQuote)));

            if (originalText.isNotEmpty())
            {
                const int openingQuote = findCloseQuote (line, closeQuote + 1);
                closeQuote = findCloseQuote (line, openingQuote + 1);

                const String newText (unescapeString (line.substring (openingQuote + 1, closeQuote)));

                if (newText.isNotEmpty())
                    translations.set (originalText, newText);
            }
        }
        else if (line.startsWithIgnoreCase ("language:"))
        {
            languageName = line.substring (9).trim();
        }
        else if (line.startsWithIgnoreCase ("countries:"))
        {
            countryCodes.addTokens (line.substring (10).trim(), true);
            countryCodes.trim();
            countryCodes.removeEmptyStrings();
        }
    }
}

//==============================================================================
void LocalisedStrings::setCurrentMappings (LocalisedStrings* newTranslations)
{
    const SpinLock::ScopedLockType sl (currentMappingsLock);
    currentMappings = newTranslations;
}

LocalisedStrings* LocalisedStrings::getCurrentMappings()
{
    return currentMappings;
}

String LocalisedStrings::translateWithCurrentMappings (const String& text)  { return beast::translate (text); }
String LocalisedStrings::translateWithCurrentMappings (const char* text)    { return beast::translate (text); }

String translate (const String& text)       { return beast::translate (text, text); }
String translate (const char* text)         { return beast::translate (String (text)); }
String translate (CharPointer_UTF8 text)    { return beast::translate (String (text)); }

String translate (const String& text, const String& resultIfNotFound)
{
    const SpinLock::ScopedLockType sl (currentMappingsLock);

    if (const LocalisedStrings* const mappings = LocalisedStrings::getCurrentMappings())
        return mappings->translate (text, resultIfNotFound);

    return resultIfNotFound;
}
