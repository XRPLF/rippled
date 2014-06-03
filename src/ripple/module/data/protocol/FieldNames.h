//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_FIELDNAMES_H
#define RIPPLE_FIELDNAMES_H

namespace ripple {

// VFALCO TODO lose the macro.
#define FIELD_CODE(type, index) ((static_cast<int>(type) << 16) | index)

enum SerializedTypeID
{
    // special types
    STI_UNKNOWN     = -2,
    STI_DONE        = -1,
    STI_NOTPRESENT  = 0,

#define TYPE(name, field, value) STI_##field = value,
#define FIELD(name, field, value)
#include <ripple/module/data/protocol/SerializeDeclarations.h>
#undef TYPE
#undef FIELD

    // high level types
    STI_TRANSACTION = 10001,
    STI_LEDGERENTRY = 10002,
    STI_VALIDATION  = 10003,
};

/** Identifies fields.

    Fields are necessary to tag data in signed transactions so that
    the binary format of the transaction can be canonicalized.
*/
// VFALCO TODO rename this to NamedField
class SField
{
public:
    typedef const SField&   ref;
    typedef SField const*   ptr;

    static const int sMD_Never          = 0x00;
    static const int sMD_ChangeOrig     = 0x01; // original value when it changes
    static const int sMD_ChangeNew      = 0x02; // new value when it changes
    static const int sMD_DeleteFinal    = 0x04; // final value when it is deleted
    static const int sMD_Create         = 0x08; // value when it's created
    static const int sMD_Always         = 0x10; // value when node containing it is affected at all
    static const int sMD_Default        = sMD_ChangeOrig | sMD_ChangeNew | sMD_DeleteFinal | sMD_Create;

public:

    const int               fieldCode;      // (type<<16)|index
    const SerializedTypeID  fieldType;      // STI_*
    const int               fieldValue;     // Code number for protocol
    std::string             fieldName;
    int                     fieldMeta;
    int                     fieldNum;
    bool                    signingField;
    std::string             rawJsonName;
    Json::StaticString      jsonName;

    SField(SField const&) = delete;
    SField& operator=(SField const&) = delete;

    SField (int fc, SerializedTypeID tid, int fv, const char* fn)
        : fieldCode (fc)
        , fieldType (tid)
        , fieldValue (fv)
        , fieldName (fn)
        , fieldMeta (sMD_Default)
        , signingField (true)
        , rawJsonName (getName ())
        , jsonName (rawJsonName.c_str ())
    {
        StaticScopedLockType sl (getMutex ());

        codeToField[fieldCode] = this;

        fieldNum = ++num;
    }

    SField (SerializedTypeID tid, int fv, const char* fn)
        : fieldCode (FIELD_CODE (tid, fv))
        , fieldType (tid)
        , fieldValue (fv)
        , fieldName (fn)
        , fieldMeta (sMD_Default)
        , signingField (true)
        , rawJsonName (getName ())
        , jsonName (rawJsonName.c_str ())
    {
        StaticScopedLockType sl (getMutex ());

        codeToField[fieldCode] = this;

        fieldNum = ++num;
    }

    explicit SField (int fc)
        : fieldCode (fc)
        , fieldType (STI_UNKNOWN)
        , fieldValue (0)
        , fieldMeta (sMD_Never)
        , signingField (true)
        , rawJsonName (getName ())
        , jsonName (rawJsonName.c_str ())
    {
        StaticScopedLockType sl (getMutex ());
        fieldNum = ++num;
    }

    ~SField ();

    static SField::ref getField (int fieldCode);
    static SField::ref getField (const std::string& fieldName);
    static SField::ref getField (int type, int value)
    {
        return getField (FIELD_CODE (type, value));
    }
    static SField::ref getField (SerializedTypeID type, int value)
    {
        return getField (FIELD_CODE (type, value));
    }

    std::string getName () const;
    bool hasName () const
    {
        return !fieldName.empty ();
    }

    Json::StaticString const& getJsonName () const
    {
        return jsonName;
    }

    bool isGeneric () const
    {
        return fieldCode == 0;
    }
    bool isInvalid () const
    {
        return fieldCode == -1;
    }
    bool isUseful () const
    {
        return fieldCode > 0;
    }
    bool isKnown () const
    {
        return fieldType != STI_UNKNOWN;
    }
    bool isBinary () const
    {
        return fieldValue < 256;
    }

    // VFALCO NOTE What is a discardable field?
    bool isDiscardable () const
    {
        return fieldValue > 256;
    }

    int getCode () const
    {
        return fieldCode;
    }
    int getNum () const
    {
        return fieldNum;
    }
    static int getNumFields ()
    {
        return num;
    }

    bool isSigningField () const
    {
        return signingField;
    }
    void notSigningField ()
    {
        signingField = false;
    }
    bool shouldMeta (int c) const
    {
        return (fieldMeta & c) != 0;
    }
    void setMeta (int c)
    {
        fieldMeta = c;
    }

    bool shouldInclude (bool withSigningField) const
    {
        return (fieldValue < 256) && (withSigningField || signingField);
    }

    bool operator== (const SField& f) const
    {
        return fieldCode == f.fieldCode;
    }

    bool operator!= (const SField& f) const
    {
        return fieldCode != f.fieldCode;
    }

    static int compare (SField::ref f1, SField::ref f2);

    // VFALCO TODO make these private
protected:
    static std::map<int, ptr>   codeToField;

    typedef RippleMutex StaticLockType;
    typedef std::lock_guard <StaticLockType> StaticScopedLockType;

    static StaticLockType& getMutex ();

    // VFALCO NOTE can this be replaced with an atomic int???!
    static int                  num;

    SField (SerializedTypeID id, int val);
};

extern SField sfInvalid, sfGeneric, sfLedgerEntry, sfTransaction, sfValidation;

#define FIELD(name, type, index) extern SField sf##name;
#define TYPE(name, type, index)
#include <ripple/module/data/protocol/SerializeDeclarations.h>
#undef FIELD
#undef TYPE

} // ripple

#endif
