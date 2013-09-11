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

#ifndef BEAST_LOCALISEDSTRINGS_H_INCLUDED
#define BEAST_LOCALISEDSTRINGS_H_INCLUDED

//==============================================================================
/**
    Used to convert strings to localised foreign-language versions.

    This is basically a look-up table of strings and their translated equivalents.
    It can be loaded from a text file, so that you can supply a set of localised
    versions of strings that you use in your app.

    To use it in your code, simply call the translate() method on each string that
    might have foreign versions, and if none is found, the method will just return
    the original string.

    The translation file should start with some lines specifying a description of
    the language it contains, and also a list of ISO country codes where it might
    be appropriate to use the file. After that, each line of the file should contain
    a pair of quoted strings with an '=' sign.

    E.g. for a french translation, the file might be:

    @code
    language: French
    countries: fr be mc ch lu

    "hello" = "bonjour"
    "goodbye" = "au revoir"
    @endcode

    If the strings need to contain a quote character, they can use '\"' instead, and
    if the first non-whitespace character on a line isn't a quote, then it's ignored,
    (you can use this to add comments).

    Note that this is a singleton class, so don't create or destroy the object directly.
    There's also a TRANS(text) macro defined to make it easy to use the this.

    E.g. @code
    printSomething (TRANS("hello"));
    @endcode

    This macro is used in the Juce classes themselves, so your application has a chance to
    intercept and translate any internal Juce text strings that might be shown. (You can easily
    get a list of all the messages by searching for the TRANS() macro in the Juce source
    code).
*/
class BEAST_API  LocalisedStrings : LeakChecked <LocalisedStrings>
{
public:
    //==============================================================================
    /** Creates a set of translations from the text of a translation file.

        When you create one of these, you can call setCurrentMappings() to make it
        the set of mappings that the system's using.
    */
    LocalisedStrings (const String& fileContents,
                      bool ignoreCaseOfKeys);

    /** Creates a set of translations from a file.

        When you create one of these, you can call setCurrentMappings() to make it
        the set of mappings that the system's using.
    */
    LocalisedStrings (const File& fileToLoad,
                      bool ignoreCaseOfKeys);

    /** Destructor. */
    ~LocalisedStrings();

    //==============================================================================
    /** Selects the current set of mappings to be used by the system.

        The object you pass in will be automatically deleted when no longer needed, so
        don't keep a pointer to it. You can also pass in zero to remove the current
        mappings.

        See also the TRANS() macro, which uses the current set to do its translation.

        @see translateWithCurrentMappings
    */
    static void setCurrentMappings (LocalisedStrings* newTranslations);

    /** Returns the currently selected set of mappings.

        This is the object that was last passed to setCurrentMappings(). It may
        be nullptr if none has been created.
    */
    static LocalisedStrings* getCurrentMappings();

    /** Tries to translate a string using the currently selected set of mappings.

        If no mapping has been set, or if the mapping doesn't contain a translation
        for the string, this will just return the original string.

        See also the TRANS() macro, which uses this method to do its translation.

        @see setCurrentMappings, getCurrentMappings
    */
    static String translateWithCurrentMappings (const String& text);

    /** Tries to translate a string using the currently selected set of mappings.

        If no mapping has been set, or if the mapping doesn't contain a translation
        for the string, this will just return the original string.

        See also the TRANS() macro, which uses this method to do its translation.

        @see setCurrentMappings, getCurrentMappings
    */
    static String translateWithCurrentMappings (const char* text);

    //==============================================================================
    /** Attempts to look up a string and return its localised version.
        If the string isn't found in the list, the original string will be returned.
    */
    String translate (const String& text) const;

    /** Attempts to look up a string and return its localised version.
        If the string isn't found in the list, the resultIfNotFound string will be returned.
    */
    String translate (const String& text, const String& resultIfNotFound) const;

    /** Returns the name of the language specified in the translation file.

        This is specified in the file using a line starting with "language:", e.g.
        @code
        language: german
        @endcode
    */
    String getLanguageName() const                        { return languageName; }

    /** Returns the list of suitable country codes listed in the translation file.

        These is specified in the file using a line starting with "countries:", e.g.
        @code
        countries: fr be mc ch lu
        @endcode

        The country codes are supposed to be 2-character ISO complient codes.
    */
    const StringArray& getCountryCodes() const            { return countryCodes; }

    /** Provides access to the actual list of mappings. */
    const StringPairArray& getMappings() const            { return translations; }

private:
    //==============================================================================
    String languageName;
    StringArray countryCodes;
    StringPairArray translations;

    void loadFromText (const String&, bool ignoreCase);
};

//==============================================================================
#ifndef TRANS
 /** Uses the LocalisedStrings class to translate the given string literal.
     This macro is provided for backwards-compatibility, and just calls the translate()
     function. In new code, it's recommended that you just call translate() directly
     instead, and avoid using macros.
     @see translate(), LocalisedStrings
 */
 #define TRANS(stringLiteral) beast::translate (stringLiteral)
#endif

/** A dummy version of the TRANS macro, used to indicate a string literal that should be
    added to the translation file by source-code scanner tools.

    Wrapping a string literal in this macro has no effect, but by using it around strings
    that your app needs to translate at a later stage, it lets automatic code-scanning tools
    find this string and add it to the list of strings that need translation.
*/
#define NEEDS_TRANS(stringLiteral) (stringLiteral)

/** Uses the LocalisedStrings class to translate the given string literal.
    @see LocalisedStrings
*/
String translate (const String& stringLiteral);

/** Uses the LocalisedStrings class to translate the given string literal.
    @see LocalisedStrings
*/
String translate (const char* stringLiteral);

/** Uses the LocalisedStrings class to translate the given string literal.
    @see LocalisedStrings
*/
String translate (CharPointer_UTF8 stringLiteral);

/** Uses the LocalisedStrings class to translate the given string literal.
    @see LocalisedStrings
*/
String translate (const String& stringLiteral, const String& resultIfNotFound);


#endif   // BEAST_LOCALISEDSTRINGS_H_INCLUDED
