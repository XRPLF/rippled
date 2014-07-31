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

namespace ripple {

// These must stay at the top of this file
std::map<int, SField::ptr> SField::knownCodeToField;
std::map<int, SField::ptr> SField::unknownCodeToField;
int SField::num = 0;


// Solve construction issues for objects with static storage duration.
SField::StaticLockType& SField::getMutex ()
{
    static StaticLockType mutex;
    return mutex;
}

SField const sfInvalid (-1), sfGeneric (0);
SField const sfLedgerEntry (STI_LEDGERENTRY, 257, "LedgerEntry");
SField const sfTransaction (STI_TRANSACTION, 257, "Transaction");
SField const sfValidation (STI_VALIDATION, 257, "Validation");
SField const sfMetadata (STI_METADATA, 257, "Metadata");
SField const sfHash (STI_HASH256, 257, "hash");
SField const sfIndex (STI_HASH256, 258, "index");

// TODO(tom): Remove this horror.
#define FIELD(name, type, index) \
    SField sf##name(FIELD_CODE(STI_##type, index), STI_##type, index, #name);
#define TYPE(name, type, index)
#include <ripple/module/data/protocol/SerializeDeclarations.h>
#undef FIELD
#undef TYPE

static int initFields ()
{
    sfTxnSignature.notSigningField ();
    sfTxnSignatures.notSigningField ();
    sfSignature.notSigningField ();

    sfIndexes.setMeta (SField::sMD_Never);
    sfPreviousTxnID.setMeta (SField::sMD_DeleteFinal);
    sfPreviousTxnLgrSeq.setMeta (SField::sMD_DeleteFinal);
    sfLedgerEntryType.setMeta (SField::sMD_Never);
    sfRootIndex.setMeta (SField::sMD_Always);

    return 0;
}

static const int forceInitializionOfFields = initFields ();

SField::SField (SerializedTypeID tid, int fv)
        : fieldCode (FIELD_CODE (tid, fv)), fieldType (tid), fieldValue (fv),
          fieldMeta (sMD_Default),
          fieldNum (++num),
          signingField (true),
          jsonName (nullptr)
{
    // call with the map mutex
    fieldName = beast::lexicalCast <std::string> (tid) + "/" +
                beast::lexicalCast <std::string> (fv);
    unknownCodeToField[fieldCode] = this;
    rawJsonName = getName ();
    jsonName = Json::StaticString (rawJsonName.c_str ());
    assert ((fv != 1) || ((tid != STI_ARRAY) && (tid != STI_OBJECT)));
}

SField::ref SField::getField (int code)
{
    std::map<int, SField::ptr>::iterator it = knownCodeToField.find (code);

    if (it != knownCodeToField.end ())
    {
        // 99+% of the time, it will be a valid, known field
        return * (it->second);
    }

    int type = code >> 16;
    int field = code % 0xffff;

    // Don't dynamically extend types that have no binary encoding.
    if ((field > 255) || (code < 0))
        return sfInvalid;

    switch (type)
    {
        // Types we are willing to dynamically extend
        // TODO(tom): Remove this horror.
#define FIELD(name, type, index)
#define TYPE(name, type, index) case STI_##type:
#include <ripple/module/data/protocol/SerializeDeclarations.h>
#undef FIELD
#undef TYPE

        break;

    default:
        return sfInvalid;
    }

    {
        StaticScopedLockType sl (getMutex ());

        it = unknownCodeToField.find (code);

        if (it != unknownCodeToField.end ())
            return * (it->second);

        return * (new SField (static_cast<SerializedTypeID> (type), field));
    }
}

int SField::compare (SField::ref f1, SField::ref f2)
{
    // -1 = f1 comes before f2, 0 = illegal combination, 1 = f1 comes after f2
    if ((f1.fieldCode <= 0) || (f2.fieldCode <= 0))
        return 0;

    if (f1.fieldCode < f2.fieldCode)
        return -1;

    if (f2.fieldCode < f1.fieldCode)
        return 1;

    return 0;
}

std::string SField::getName () const
{
    if (!fieldName.empty ())
        return fieldName;

    if (fieldValue == 0)
        return "";

    return std::to_string(static_cast<int> (fieldType)) + "/" +
            std::to_string(fieldValue);
}

SField::ref SField::getField (std::string const& fieldName)
{
    // OPTIMIZEME me with a map. CHECKME this is case sensitive
    for (auto const & fieldPair : knownCodeToField)
    {
        if (fieldPair.second->fieldName == fieldName)
            return * (fieldPair.second);
    }

    {
        StaticScopedLockType sl (getMutex ());

        for (auto const & fieldPair : unknownCodeToField)
        {
            if (fieldPair.second->fieldName == fieldName)
                return * (fieldPair.second);
        }
    }


    return sfInvalid;
}

SField::~SField ()
{
    std::map<int, ptr>::iterator it = knownCodeToField.find (fieldCode);

    if ((it != knownCodeToField.end ()) && (it->second == this))
    {
        knownCodeToField.erase (it);
    }
    else
    {
        StaticScopedLockType sl (getMutex ());

        it = unknownCodeToField.find (fieldCode);

        if ((it != unknownCodeToField.end ()) && (it->second == this))
            unknownCodeToField.erase (it);
    }
}

} // ripple
