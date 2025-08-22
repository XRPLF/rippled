//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2015 Ripple Labs Inc.

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

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/STParsedJSON.h>

namespace ripple {

class STParsedJSON_test : public beast::unit_test::suite
{
    void
    testUInt8()
    {
        Json::Value j;
        j[sfCloseResolution] = 42;
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfCloseResolution));
        BEAST_EXPECT(obj.object->getFieldU8(sfCloseResolution) == 42);
    }

    void
    testUInt16()
    {
        Json::Value j;
        j[sfLedgerEntryType] = 65535;
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfLedgerEntryType));
        BEAST_EXPECT(obj.object->getFieldU16(sfLedgerEntryType) == 65535);
    }

    void
    testUInt32()
    {
        Json::Value j;
        j[sfNetworkID] = 4294967295u;
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfNetworkID));
        BEAST_EXPECT(obj.object->getFieldU32(sfNetworkID) == 4294967295u);
    }

    void
    testUInt64()
    {
        Json::Value j;
        j[sfIndexNext] = "abcdefabcdef";
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfIndexNext));
        BEAST_EXPECT(
            obj.object->getFieldU64(sfIndexNext) == 188900977659375ull);
    }

    void
    testInt32()
    {
        Json::Value j;
        j[sfWasmReturnCode] = -123456789;
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfWasmReturnCode));
        BEAST_EXPECT(obj.object->getFieldI32(sfWasmReturnCode) == -123456789);
    }

    void
    testBlob()
    {
        Json::Value j;
        j[sfPublicKey] = "DEADBEEF";
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfPublicKey));
        auto const& blob = obj.object->getFieldVL(sfPublicKey);
        BEAST_EXPECT(blob.size() == 4);
        BEAST_EXPECT(blob[0] == 0xDE);
        BEAST_EXPECT(blob[1] == 0xAD);
        BEAST_EXPECT(blob[2] == 0xBE);
        BEAST_EXPECT(blob[3] == 0xEF);
    }

    void
    testVector256()
    {
        Json::Value j;
        Json::Value arr(Json::arrayValue);
        arr.append(
            "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF");
        arr.append(
            "FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210");
        j[sfHashes] = arr;
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfHashes));
        auto const& vec = obj.object->getFieldV256(sfHashes);
        BEAST_EXPECT(vec.size() == 2);
        BEAST_EXPECT(vec[0].size() == 32);
        BEAST_EXPECT(vec[1].size() == 32);
    }

    void
    testAccount()
    {
        Json::Value j;
        j[sfAccount] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfAccount));
        auto const& acct = obj.object->getAccountID(sfAccount);
        BEAST_EXPECT(acct.size() == 20);
    }

    void
    testCurrency()
    {
        Json::Value j;
        j[sfBaseAsset] = "USD";
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfBaseAsset));
        auto const& curr = obj.object->getFieldCurrency(sfBaseAsset);
        BEAST_EXPECT(curr.currency().size() == 20);
    }

    void
    testHash128()
    {
        Json::Value j;
        j[sfEmailHash] = "0123456789ABCDEF0123456789ABCDEF";
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfEmailHash));
        BEAST_EXPECT(obj.object->getFieldH128(sfEmailHash).size() == 16);
    }

    void
    testHash160()
    {
        Json::Value j;
        j[sfTakerPaysCurrency] = "0123456789ABCDEF0123456789ABCDEF01234567";
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfTakerPaysCurrency));
        BEAST_EXPECT(
            obj.object->getFieldH160(sfTakerPaysCurrency).size() == 20);
    }

    void
    testHash256()
    {
        Json::Value j;
        j[sfLedgerHash] =
            "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfLedgerHash));
        BEAST_EXPECT(obj.object->getFieldH256(sfLedgerHash).size() == 32);
    }

    void
    testAmount()
    {
        Json::Value j;
        j[sfAmount] = "1000000";
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfAmount));
        BEAST_EXPECT(obj.object->getFieldAmount(sfAmount) == STAmount(1000000));
    }

    void
    testPathSet()
    {
        Json::Value j;
        Json::Value path(Json::arrayValue);
        Json::Value elem(Json::objectValue);
        elem["account"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
        path.append(elem);
        Json::Value pathset(Json::arrayValue);
        pathset.append(path);
        j[sfPaths] = pathset;
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfPaths));
        auto const& ps = obj.object->getFieldPathSet(sfPaths);
        BEAST_EXPECT(!ps.empty());
    }

    void
    testObject()
    {
        Json::Value j;
        Json::Value objVal(Json::objectValue);
        objVal[sfTransactionResult] = 1;
        j[sfTransactionMetaData] = objVal;
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfTransactionMetaData));
    }

    void
    testArray()
    {
        Json::Value j;
        Json::Value arr(Json::arrayValue);
        Json::Value elem(Json::objectValue);
        elem[sfTransactionResult] = 2;
        Json::Value elem2(Json::objectValue);
        elem2[sfTransactionMetaData] = elem;
        arr.append(elem2);
        j[sfSignerEntries] = arr;
        STParsedJSONObject obj("Test", j);
        BEAST_EXPECT(obj.object.has_value());
        BEAST_EXPECT(obj.object->isFieldPresent(sfSignerEntries));
    }

    void
    run() override
    {
        testUInt8();
        testUInt16();
        testUInt32();
        testUInt64();
        testInt32();
        testBlob();
        testVector256();
        testAccount();
        testCurrency();
        testHash128();
        testHash160();
        testHash256();
        testAmount();
        testPathSet();
        testObject();
        testArray();
    }
};

BEAST_DEFINE_TESTSUITE(STParsedJSON, protocol, ripple);

}  // namespace ripple
