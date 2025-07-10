//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TypedLedgerEntries.h>

namespace ripple {

struct TypedLedgerEntries_test : public beast::unit_test::suite
{
    void
    testAccessSTArrayProxy()
    {
        STArray innerArray;
        STArrayProxy<LedgerObjectType<ltSIGNER_LIST>> array{&innerArray};

        BEAST_EXPECT(array.empty());
        auto item = array.createItem();
        item.fsfOwnerNode() = 1;
        array.push_back(item);

        BEAST_EXPECT(array.back().fsfOwnerNode() == 1);

        BEAST_EXPECT(array.value().back()[sfOwnerNode] == 1);

        BEAST_EXPECT(array.begin()->fsfOwnerNode() == 1);
        BEAST_EXPECT(std::distance(array.begin(), array.end()) == 1);
        BEAST_EXPECT(array.size() == 1);
        BEAST_EXPECT(!array.empty());
        BEAST_EXPECT(array.at(0).fsfOwnerNode() == 1);
        BEAST_EXPECT(!array.at(1).isValid());
        BEAST_EXPECT(array.valid());
        BEAST_EXPECT(
            !STArrayProxy<LedgerObjectType<ltSIGNER_LIST>>{nullptr}.valid());
    }

    void
    testGet()
    {
        testcase("testGet");

        auto object = std::make_shared<SLE>(ltSIGNER_LIST, uint256{});
        STArray signerEntries;
        STObject signerEntry{sfSignerEntry};
        signerEntry[sfAccount] = AccountID{1};
        signerEntry[sfSignerWeight] = 2;
        signerEntry[sfWalletLocator] = uint256{3};
        signerEntries.emplace_back(signerEntry);

        (*object)[sfOwnerNode] = 1UL;
        (*object)[sfSignerQuorum] = 2U;
        (*object)[sfSignerListID] = 3U;
        (*object)[sfPreviousTxnLgrSeq] = 4U;
        (*object)[sfPreviousTxnID] = uint256{5};
        (*object).setFieldArray(sfSignerEntries, signerEntries);

        auto entry = LedgerObjectType<ltSIGNER_LIST>::fromObject(object);
        BEAST_EXPECT(entry.fsfOwnerNode() == 1);
        BEAST_EXPECT(entry.fsfSignerQuorum() == 2);
        BEAST_EXPECT(entry.fsfSignerListID() == 3);
        BEAST_EXPECT(entry.fsfPreviousTxnLgrSeq() == 4);
        BEAST_EXPECT(entry.fsfPreviousTxnID() == uint256{5});
        BEAST_EXPECT(entry.fsfSignerEntries().size() == 1);

        BEAST_EXPECT(
            entry.fsfSignerEntries()[0].fsfAccount().value() == AccountID{1});
        BEAST_EXPECT(entry.fsfSignerEntries()[0].fsfSignerWeight() == 2);
        BEAST_EXPECT(
            entry.fsfSignerEntries()[0].fsfWalletLocator() == uint256{3});
    }

    void
    testSet()
    {
        testcase("testSet");
        auto new_object = LedgerObjectType<ltSIGNER_LIST>::create(uint256{});

        new_object.fsfOwnerNode() = 1;
        new_object.fsfSignerQuorum() = 2;
        new_object.fsfSignerListID() = 3;
        new_object.fsfPreviousTxnLgrSeq() = 4;
        new_object.fsfPreviousTxnID() = uint256{5};
        auto signerEntry = new_object.fsfSignerEntries().createItem();
        signerEntry.fsfAccount() = AccountID{1};
        signerEntry.fsfSignerWeight() = 2;
        signerEntry.fsfWalletLocator() = uint256{3};
        new_object.fsfSignerEntries().push_back(signerEntry);

        auto& object = *new_object.getObject();

        BEAST_EXPECT(object[sfOwnerNode] == new_object.fsfOwnerNode());
        BEAST_EXPECT(object[sfSignerQuorum] == new_object.fsfSignerQuorum());
        BEAST_EXPECT(object[sfSignerListID] == new_object.fsfSignerListID());
        BEAST_EXPECT(
            object[sfPreviousTxnLgrSeq] == new_object.fsfPreviousTxnLgrSeq());
        BEAST_EXPECT(object[sfPreviousTxnID] == new_object.fsfPreviousTxnID());
        BEAST_EXPECT(
            object.getFieldArray(sfSignerEntries).size() ==
            new_object.fsfSignerEntries().size());

        auto entries = object.getFieldArray(sfSignerEntries);
        BEAST_EXPECT(
            entries[0][sfAccount] ==
            new_object.fsfSignerEntries()[0].fsfAccount());
        BEAST_EXPECT(
            entries[0][sfSignerWeight] ==
            new_object.fsfSignerEntries()[0].fsfSignerWeight());
        BEAST_EXPECT(
            entries[0][sfWalletLocator] ==
            new_object.fsfSignerEntries()[0].fsfWalletLocator());
    }

    void
    run() override
    {
        testGet();
        testSet();
    }
};

BEAST_DEFINE_TESTSUITE(TypedLedgerEntries, protocol, ripple);
}  // namespace ripple
