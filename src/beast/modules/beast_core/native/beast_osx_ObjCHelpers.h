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

#ifndef BEAST_OSX_OBJCHELPERS_H_INCLUDED
#define BEAST_OSX_OBJCHELPERS_H_INCLUDED


/* This file contains a few helper functions that are used internally but which
   need to be kept away from the public headers because they use obj-C symbols.
*/
namespace
{
    //==============================================================================
    static inline String nsStringToBeast (NSString* s)
    {
        return CharPointer_UTF8 ([s UTF8String]);
    }

    static inline NSString* beastStringToNS (const String& s)
    {
        return [NSString stringWithUTF8String: s.toUTF8()];
    }

    static inline NSString* nsStringLiteral (const char* const s) noexcept
    {
        return [NSString stringWithUTF8String: s];
    }

    static inline NSString* nsEmptyString() noexcept
    {
        return [NSString string];
    }
}

//==============================================================================
template <typename ObjectType>
struct NSObjectRetainer
{
    inline NSObjectRetainer (ObjectType* o) : object (o)  { [object retain]; }
    inline ~NSObjectRetainer()                            { [object release]; }

    ObjectType* object;
};

//==============================================================================
template <typename SuperclassType>
struct ObjCClass : public Uncopyable
{
    ObjCClass (const char* nameRoot)
        : cls (objc_allocateClassPair ([SuperclassType class], getRandomisedName (nameRoot).toUTF8(), 0))
    {
    }

    ~ObjCClass()
    {
        objc_disposeClassPair (cls);
    }

    void registerClass()
    {
        objc_registerClassPair (cls);
    }

    SuperclassType* createInstance() const
    {
        return class_createInstance (cls, 0);
    }

    template <typename Type>
    void addIvar (const char* name)
    {
        BOOL b = class_addIvar (cls, name, sizeof (Type), (uint8_t) rint (log2 (sizeof (Type))), @encode (Type));
        bassert (b); (void) b;
    }

    template <typename FunctionType>
    void addMethod (SEL selector, FunctionType callbackFn, const char* signature)
    {
        BOOL b = class_addMethod (cls, selector, (IMP) callbackFn, signature);
        bassert (b); (void) b;
    }

    template <typename FunctionType>
    void addMethod (SEL selector, FunctionType callbackFn, const char* sig1, const char* sig2)
    {
        addMethod (selector, callbackFn, (String (sig1) + sig2).toUTF8());
    }

    template <typename FunctionType>
    void addMethod (SEL selector, FunctionType callbackFn, const char* sig1, const char* sig2, const char* sig3)
    {
        addMethod (selector, callbackFn, (String (sig1) + sig2 + sig3).toUTF8());
    }

    template <typename FunctionType>
    void addMethod (SEL selector, FunctionType callbackFn, const char* sig1, const char* sig2, const char* sig3, const char* sig4)
    {
        addMethod (selector, callbackFn, (String (sig1) + sig2 + sig3 + sig4).toUTF8());
    }

    void addProtocol (Protocol* protocol)
    {
        BOOL b = class_addProtocol (cls, protocol);
        bassert (b); (void) b;
    }

    static id sendSuperclassMessage (id self, SEL selector)
    {
        objc_super s = { self, [SuperclassType class] };
        return objc_msgSendSuper (&s, selector);
    }

    template <typename Type>
    static Type getIvar (id self, const char* name)
    {
        void* v = nullptr;
        object_getInstanceVariable (self, name, &v);
        return static_cast <Type> (v);
    }

    Class cls;

private:
    static String getRandomisedName (const char* root)
    {
        return root + String::toHexString (beast::Random::getSystemRandom().nextInt64());
    }
};


#endif   // BEAST_OSX_OBJCHELPERS_H_INCLUDED
