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

#include <test/app/wasm_fixtures/fixtures.h>
#include <test/jtx.h>

#include <xrpld/app/misc/WasmHostFunc.h>
#include <xrpld/app/misc/WasmVM.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/ledger/detail/ApplyViewBase.h>

#include <wasm_c_api.h>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

namespace ripple {
namespace test {

using Add_proto = int32_t(int32_t, int32_t);
static wasm_trap_t*
Add(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    int32_t Val1 = params->data[0].of.i32;
    int32_t Val2 = params->data[1].of.i32;
    // printf("Host function \"Add\": %d + %d\n", Val1, Val2);
    results->data[0] = WASM_I32_VAL(Val1 + Val2);
    return nullptr;
}

struct TestLedgerDataProvider
{
    jtx::Env* env;

public:
    TestLedgerDataProvider(jtx::Env* env_) : env(env_)
    {
    }

    Expected<int32_t, HostFuncError>
    get_ledger_sqn()
    {
        return (int32_t)env->current()->seq();
    }
};

using getLedgerSqn_proto = std::int32_t();
static wasm_trap_t*
getLedgerSqn_wrap(void* env, wasm_val_vec_t const*, wasm_val_vec_t* results)
{
    auto sqn = reinterpret_cast<TestLedgerDataProvider*>(env)->get_ledger_sqn();

    results->data[0] = WASM_I32_VAL(sqn.value());
    results->num_elems = 1;

    return nullptr;
}

struct TestHostFunctions : public HostFunctions
{
    test::jtx::Env& env_;
    AccountID accountID_;
    Bytes data_;
    int clock_drift_ = 0;
    void const* rt_ = nullptr;

public:
    TestHostFunctions(test::jtx::Env& env, int cd = 0)
        : env_(env), clock_drift_(cd)
    {
        auto opt = parseBase58<AccountID>("rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh");
        if (opt)
            accountID_ = *opt;
        std::string t = "10000";
        data_ = Bytes{t.begin(), t.end()};
    }

    virtual void
    setRT(void const* rt) override
    {
        rt_ = rt;
    }

    virtual void const*
    getRT() const override
    {
        return rt_;
    }

    beast::Journal
    getJournal() override
    {
        return env_.journal;
    }

    Expected<int32_t, HostFuncError>
    getLedgerSqn() override
    {
        return static_cast<int32_t>(env_.current()->seq());
    }

    Expected<int32_t, HostFuncError>
    getParentLedgerTime() override
    {
        return env_.current()->parentCloseTime().time_since_epoch().count() +
            clock_drift_;
    }

    Expected<Hash, HostFuncError>
    getParentLedgerHash() override
    {
        return env_.current()->info().parentHash;
    }

    virtual Expected<int32_t, HostFuncError>
    cacheLedgerObj(uint256 const& objId, int32_t cacheIdx) override
    {
        return 1;
    }

    Expected<Bytes, HostFuncError>
    getTxField(SField const& fname) override
    {
        if (fname == sfAccount)
            return Bytes(accountID_.begin(), accountID_.end());
        else if (fname == sfFee)
        {
            int64_t x = 235;
            uint8_t const* p = reinterpret_cast<uint8_t const*>(&x);
            return Bytes{p, p + sizeof(x)};
        }
        else if (fname == sfSequence)
        {
            auto const x = getLedgerSqn();
            if (!x)
                return Unexpected(x.error());
            uint8_t const* p = reinterpret_cast<uint8_t const*>(&x.value());
            return Bytes{p, p + sizeof(x)};
        }
        return Bytes();
    }

    Expected<Bytes, HostFuncError>
    getCurrentLedgerObjField(SField const& fname) override
    {
        auto const& sn = fname.getName();
        if (sn == "Destination" || sn == "Account")
            return Bytes(accountID_.begin(), accountID_.end());
        else if (sn == "Data")
            return data_;
        else if (sn == "FinishAfter")
        {
            auto t =
                env_.current()->parentCloseTime().time_since_epoch().count();
            std::string s = std::to_string(t);
            return Bytes{s.begin(), s.end()};
        }

        return Unexpected(HF_ERR_INTERNAL);
    }

    Expected<Bytes, HostFuncError>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) override
    {
        // auto const& sn = fname.getName();
        if (fname == sfBalance)
        {
            int64_t x = 10'000;
            uint8_t const* p = reinterpret_cast<uint8_t const*>(&x);
            return Bytes{p, p + sizeof(x)};
        }
        return data_;
    }

    Expected<Bytes, HostFuncError>
    getTxNestedField(Bytes const& locator) override
    {
        uint8_t const a[] = {0x2b, 0x6a, 0x23, 0x2a, 0xa4, 0xc4, 0xbe, 0x41,
                             0xbf, 0x49, 0xd2, 0x45, 0x9f, 0xa4, 0xa0, 0x34,
                             0x7e, 0x1b, 0x54, 0x3a, 0x4c, 0x92, 0xfc, 0xee,
                             0x08, 0x21, 0xc0, 0x20, 0x1e, 0x2e, 0x9a, 0x00};
        return Bytes(&a[0], &a[sizeof(a)]);
    }

    Expected<Bytes, HostFuncError>
    getCurrentLedgerObjNestedField(Bytes const& locator) override
    {
        uint8_t const a[] = {0x2b, 0x6a, 0x23, 0x2a, 0xa4, 0xc4, 0xbe, 0x41,
                             0xbf, 0x49, 0xd2, 0x45, 0x9f, 0xa4, 0xa0, 0x34,
                             0x7e, 0x1b, 0x54, 0x3a, 0x4c, 0x92, 0xfc, 0xee,
                             0x08, 0x21, 0xc0, 0x20, 0x1e, 0x2e, 0x9a, 0x00};
        return Bytes(&a[0], &a[sizeof(a)]);
    }

    Expected<Bytes, HostFuncError>
    getLedgerObjNestedField(int32_t cacheIdx, Bytes const& locator) override
    {
        uint8_t const a[] = {0x2b, 0x6a, 0x23, 0x2a, 0xa4, 0xc4, 0xbe, 0x41,
                             0xbf, 0x49, 0xd2, 0x45, 0x9f, 0xa4, 0xa0, 0x34,
                             0x7e, 0x1b, 0x54, 0x3a, 0x4c, 0x92, 0xfc, 0xee,
                             0x08, 0x21, 0xc0, 0x20, 0x1e, 0x2e, 0x9a, 0x00};
        return Bytes(&a[0], &a[sizeof(a)]);
    }

    Expected<int32_t, HostFuncError>
    getTxArrayLen(SField const& fname) override
    {
        return 32;
    }

    Expected<int32_t, HostFuncError>
    getCurrentLedgerObjArrayLen(SField const& fname) override
    {
        return 32;
    }

    Expected<int32_t, HostFuncError>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname) override
    {
        return 32;
    }

    Expected<int32_t, HostFuncError>
    getTxNestedArrayLen(Bytes const& locator) override
    {
        return 32;
    }

    Expected<int32_t, HostFuncError>
    getCurrentLedgerObjNestedArrayLen(Bytes const& locator) override
    {
        return 32;
    }

    Expected<int32_t, HostFuncError>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, Bytes const& locator) override
    {
        return 32;
    }

    Expected<int32_t, HostFuncError>
    updateData(Bytes const& data) override
    {
        return 0;
    }

    Expected<Hash, HostFuncError>
    computeSha512HalfHash(Bytes const& data) override
    {
        return env_.current()->info().parentHash;
    }

    Expected<Bytes, HostFuncError>
    accountKeylet(AccountID const& account) override
    {
        if (!account)
            return Unexpected(HF_ERR_INVALID_ACCOUNT);
        auto const keylet = keylet::account(account);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFuncError>
    credentialKeylet(
        AccountID const& subject,
        AccountID const& issuer,
        Bytes const& credentialType) override
    {
        if (!subject || !issuer || credentialType.empty())
            return Unexpected(HF_ERR_INVALID_ACCOUNT);
        auto const keylet =
            keylet::credential(subject, issuer, makeSlice(credentialType));
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFuncError>
    escrowKeylet(AccountID const& account, std::uint32_t seq) override
    {
        if (!account)
            return Unexpected(HF_ERR_INVALID_ACCOUNT);
        auto const keylet = keylet::escrow(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFuncError>
    oracleKeylet(AccountID const& account, std::uint32_t documentId) override
    {
        if (!account)
            return Unexpected(HF_ERR_INVALID_ACCOUNT);
        auto const keylet = keylet::oracle(account, documentId);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFuncError>
    getNFT(AccountID const& account, uint256 const& nftId) override
    {
        if (!account || !nftId)
        {
            return Unexpected(HF_ERR_INVALID_PARAMS);
        }

        std::string s = "https://ripple.com";
        return Bytes(s.begin(), s.end());
    }

    Expected<int32_t, HostFuncError>
    trace(std::string const& msg, Bytes const& data, bool asHex) override
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        auto j = getJournal().trace();
#endif
        j << msg;
        if (!asHex)
            j << std::string_view(
                reinterpret_cast<char const*>(data.data()), data.size());
        else
        {
            auto const hex =
                boost::algorithm::hex(std::string(data.begin(), data.end()));
            j << hex;
        }

#ifdef DEBUG_OUTPUT
        j << std::endl;
#endif

        return msg.size() + data.size() * (asHex ? 2 : 1);
    }

    Expected<int32_t, HostFuncError>
    traceNum(std::string const& msg, int64_t data) override
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        auto j = getJournal().trace();
#endif
        j << msg << data;

#ifdef DEBUG_OUTPUT
        j << std::endl;
#endif
        return msg.size() + sizeof(data);
    }
};

struct TestHostFunctionsSink : public TestHostFunctions
{
    test::StreamSink sink_;
    beast::Journal jlog_;
    void const* rt_ = nullptr;

public:
    explicit TestHostFunctionsSink(test::jtx::Env& env, int cd = 0)
        : TestHostFunctions(env, cd)
        , sink_(beast::severities::kDebug)
        , jlog_(sink_)
    {
    }

    test::StreamSink&
    getSink()
    {
        return sink_;
    }

    beast::Journal
    getJournal() override
    {
        return jlog_;
    }
};

struct PerfHostFunctions : public HostFunctions
{
    test::jtx::Env& env_;

    Keylet leKey;
    static int constexpr MAX_CACHE = 256;
    std::array<std::shared_ptr<SLE const>, MAX_CACHE> cache;
    std::shared_ptr<STTx const> tx_;

    void const* rt_ = nullptr;

public:
    PerfHostFunctions(
        test::jtx::Env& env,
        Keylet const& k,
        std::shared_ptr<STTx const>&& tx)
        : env_(env), leKey(k), tx_(std::move(tx))
    {
    }

    virtual void
    setRT(void const* rt) override
    {
        rt_ = rt;
    }

    virtual void const*
    getRT() const override
    {
        return rt_;
    }

    beast::Journal
    getJournal() override
    {
        return env_.journal;
    }

    Expected<int32_t, HostFuncError>
    getLedgerSqn() override
    {
        return static_cast<int32_t>(env_.current()->seq());
    }

    Expected<int32_t, HostFuncError>
    getParentLedgerTime() override
    {
        return env_.current()->parentCloseTime().time_since_epoch().count();
    }

    Expected<Hash, HostFuncError>
    getParentLedgerHash() override
    {
        return env_.current()->info().parentHash;
    }

    virtual Expected<int32_t, HostFuncError>
    cacheLedgerObj(uint256 const&, int32_t cacheIdx) override
    {
        static int32_t intIdx = 0;

        if (cacheIdx < 0 || cacheIdx > MAX_CACHE)
            return HF_ERR_SLOT_OUT_RANGE;

        if (!cacheIdx)
        {
            for (cacheIdx = 0; cacheIdx < MAX_CACHE; ++cacheIdx)
                if (!cache[cacheIdx])
                    break;
            if (cacheIdx >= MAX_CACHE)
                cacheIdx = intIdx++ % MAX_CACHE;
        }
        else
            --cacheIdx;

        if (cacheIdx >= MAX_CACHE)
            return HF_ERR_SLOTS_FULL;

        cache[cacheIdx] = env_.le(leKey);
        return cache[cacheIdx] ? cacheIdx + 1 : HF_ERR_LEDGER_OBJ_NOT_FOUND;
    }

    static Bytes
    getAnyFieldData(STBase const& obj)
    {
        // auto const& fname = obj.getFName();
        auto const stype = obj.getSType();
        switch (stype)
        {
            case STI_UNKNOWN:
            case STI_NOTPRESENT:
                return {};
                break;
            case STI_ACCOUNT: {
                auto const& super(static_cast<STAccount const&>(obj));
                auto const& data = super.value();
                return {data.begin(), data.end()};
            }
            break;
            case STI_AMOUNT: {
                auto const& super(static_cast<STAmount const&>(obj));
                int64_t const data = super.xrp().drops();
                auto const* b = reinterpret_cast<uint8_t const*>(&data);
                auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
                return {b, e};
            }
            break;
            case STI_VL: {
                auto const& super(static_cast<STBlob const&>(obj));
                auto const& data = super.value();
                return {data.begin(), data.end()};
            }
            break;
            case STI_UINT256: {
                auto const& super(static_cast<STBitString<256> const&>(obj));
                auto const& data = super.value();
                return {data.begin(), data.end()};
            }
            break;
            case STI_UINT32: {
                auto const& super(
                    static_cast<STInteger<std::uint32_t> const&>(obj));
                std::uint32_t const data = super.value();
                auto const* b = reinterpret_cast<uint8_t const*>(&data);
                auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
                return {b, e};
            }
            break;
            default:
                break;
        }

        Serializer msg;
        obj.add(msg);

        return msg.getData();
    }

    Expected<Bytes, HostFuncError>
    getTxField(SField const& fname) override
    {
        auto const* field = tx_->peekAtPField(fname);
        if (!field)
            return Unexpected(HF_ERR_FIELD_NOT_FOUND);
        if ((STI_OBJECT == field->getSType()) ||
            (STI_ARRAY == field->getSType()))
            return Unexpected(HF_ERR_NOT_LEAF_FIELD);

        return getAnyFieldData(*field);
    }

    Expected<Bytes, HostFuncError>
    getCurrentLedgerObjField(SField const& fname) override
    {
        auto const sle = env_.le(leKey);
        if (!sle)
            return Unexpected(HF_ERR_LEDGER_OBJ_NOT_FOUND);

        auto const* field = sle->peekAtPField(fname);
        if (!field)
            return Unexpected(HF_ERR_FIELD_NOT_FOUND);
        if ((STI_OBJECT == field->getSType()) ||
            (STI_ARRAY == field->getSType()))
            return Unexpected(HF_ERR_NOT_LEAF_FIELD);

        return getAnyFieldData(*field);
    }

    Expected<Bytes, HostFuncError>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) override
    {
        --cacheIdx;
        if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
            return Unexpected(HF_ERR_SLOT_OUT_RANGE);

        if (!cache[cacheIdx])
        {
            // return Unexpected(HF_ERR_INVALID_SLOT);
            cache[cacheIdx] = env_.le(leKey);
        }

        auto const* field = cache[cacheIdx]->peekAtPField(fname);
        if (!field)
            return Unexpected(HF_ERR_FIELD_NOT_FOUND);
        if ((STI_OBJECT == field->getSType()) ||
            (STI_ARRAY == field->getSType()))
            return Unexpected(HF_ERR_NOT_LEAF_FIELD);

        return getAnyFieldData(*field);
    }

    static Expected<STBase const*, HostFuncError>
    locateField(STObject const& obj, Bytes const& loc)
    {
        if (loc.empty() || (loc.size() & 3))  // must be multiple of 4
            return Unexpected(HF_ERR_LOCATOR_MALFORMED);

        int32_t const* l = reinterpret_cast<int32_t const*>(loc.data());
        int32_t const sz = loc.size() / 4;
        STBase const* field = nullptr;
        auto const& m = SField::getKnownCodeToField();

        {
            int32_t const c = l[0];
            auto const it = m.find(c);
            if (it == m.end())
                return Unexpected(HF_ERR_LOCATOR_MALFORMED);
            auto const& fname(*it->second);

            field = obj.peekAtPField(fname);
            if (!field || (STI_NOTPRESENT == field->getSType()))
                return Unexpected(HF_ERR_FIELD_NOT_FOUND);
        }

        for (int i = 1; i < sz; ++i)
        {
            int32_t const c = l[i];

            if (STI_ARRAY == field->getSType())
            {
                auto const* arr = static_cast<STArray const*>(field);
                if (c >= arr->size())
                    return Unexpected(HF_ERR_LOCATOR_MALFORMED);
                field = &(arr->operator[](c));
            }
            else if (STI_OBJECT == field->getSType())
            {
                auto const* o = static_cast<STObject const*>(field);

                auto const it = m.find(c);
                if (it == m.end())
                    return Unexpected(HF_ERR_LOCATOR_MALFORMED);
                auto const& fname(*it->second);

                field = o->peekAtPField(fname);
            }
            else  // simple field must be the last one
            {
                return Unexpected(HF_ERR_LOCATOR_MALFORMED);
            }

            if (!field || (STI_NOTPRESENT == field->getSType()))
                return Unexpected(HF_ERR_FIELD_NOT_FOUND);
        }

        return field;
    }

    Expected<Bytes, HostFuncError>
    getTxNestedField(Bytes const& locator) override
    {
        // std::cout << tx_->getJson(JsonOptions::none).toStyledString() <<
        // std::endl;
        auto const r = locateField(*tx_, locator);
        if (!r)
            return Unexpected(r.error());

        auto const* field = r.value();
        if ((STI_OBJECT == field->getSType()) ||
            (STI_ARRAY == field->getSType()))
            return Unexpected(HF_ERR_NOT_LEAF_FIELD);

        return getAnyFieldData(*field);
    }

    Expected<Bytes, HostFuncError>
    getCurrentLedgerObjNestedField(Bytes const& locator) override
    {
        auto const sle = env_.le(leKey);
        if (!sle)
            return Unexpected(HF_ERR_LEDGER_OBJ_NOT_FOUND);

        auto const r = locateField(*sle, locator);
        if (!r)
            return Unexpected(r.error());

        auto const* field = r.value();
        if ((STI_OBJECT == field->getSType()) ||
            (STI_ARRAY == field->getSType()))
            return Unexpected(HF_ERR_NOT_LEAF_FIELD);

        return getAnyFieldData(*field);
    }

    Expected<Bytes, HostFuncError>
    getLedgerObjNestedField(int32_t cacheIdx, Bytes const& locator) override
    {
        --cacheIdx;
        if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
            return Unexpected(HF_ERR_SLOT_OUT_RANGE);

        if (!cache[cacheIdx])
        {
            // return Unexpected(HF_ERR_INVALID_SLOT);
            cache[cacheIdx] = env_.le(leKey);
        }

        auto const r = locateField(*cache[cacheIdx], locator);
        if (!r)
            return Unexpected(r.error());

        auto const* field = r.value();
        if ((STI_OBJECT == field->getSType()) ||
            (STI_ARRAY == field->getSType()))
            return Unexpected(HF_ERR_NOT_LEAF_FIELD);

        return getAnyFieldData(*field);
    }

    Expected<int32_t, HostFuncError>
    getTxArrayLen(SField const& fname) override
    {
        if (fname.fieldType != STI_ARRAY)
            return HF_ERR_NO_ARRAY;

        auto const* field = tx_->peekAtPField(fname);
        if (!field)
            return HF_ERR_FIELD_NOT_FOUND;

        if (field->getSType() != STI_ARRAY)
            return HF_ERR_NO_ARRAY;
        int32_t const sz = static_cast<STArray const*>(field)->size();

        return sz;
    }

    Expected<int32_t, HostFuncError>
    getCurrentLedgerObjArrayLen(SField const& fname) override
    {
        if (fname.fieldType != STI_ARRAY)
            return HF_ERR_NO_ARRAY;

        auto const sle = env_.le(leKey);
        if (!sle)
            return HF_ERR_LEDGER_OBJ_NOT_FOUND;

        auto const* field = sle->peekAtPField(fname);
        if (!field)
            return HF_ERR_FIELD_NOT_FOUND;

        if (field->getSType() != STI_ARRAY)
            return HF_ERR_NO_ARRAY;
        int32_t const sz = static_cast<STArray const*>(field)->size();

        return sz;
    }

    Expected<int32_t, HostFuncError>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname) override
    {
        if (fname.fieldType != STI_ARRAY)
            return HF_ERR_NO_ARRAY;

        if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
            return HF_ERR_SLOT_OUT_RANGE;

        if (!cache[cacheIdx])
        {
            // return Unexpected(HF_ERR_INVALID_SLOT);
            cache[cacheIdx] = env_.le(leKey);
        }

        auto const* field = cache[cacheIdx]->peekAtPField(fname);
        if (!field)
            return HF_ERR_FIELD_NOT_FOUND;

        if (field->getSType() != STI_ARRAY)
            return HF_ERR_NO_ARRAY;
        int32_t const sz = static_cast<STArray const*>(field)->size();

        return sz;
    }

    Expected<int32_t, HostFuncError>
    getTxNestedArrayLen(Bytes const& locator) override
    {
        auto const r = locateField(*tx_, locator);
        if (!r)
            return r.error();
        auto const* field = r.value();

        if (field->getSType() != STI_ARRAY)
            return HF_ERR_NO_ARRAY;
        int32_t const sz = static_cast<STArray const*>(field)->size();

        return sz;
    }

    Expected<int32_t, HostFuncError>
    getCurrentLedgerObjNestedArrayLen(Bytes const& locator) override
    {
        auto const sle = env_.le(leKey);
        if (!sle)
            return HF_ERR_LEDGER_OBJ_NOT_FOUND;
        auto const r = locateField(*sle, locator);
        if (!r)
            return r.error();
        auto const* field = r.value();

        if (field->getSType() != STI_ARRAY)
            return HF_ERR_NO_ARRAY;
        int32_t const sz = static_cast<STArray const*>(field)->size();

        return sz;
    }

    Expected<int32_t, HostFuncError>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, Bytes const& locator) override
    {
        --cacheIdx;
        if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
            return HF_ERR_SLOT_OUT_RANGE;

        if (!cache[cacheIdx])
        {
            // return Unexpected(HF_ERR_INVALID_SLOT);
            cache[cacheIdx] = env_.le(leKey);
        }

        auto const r = locateField(*cache[cacheIdx], locator);
        if (!r)
            return r.error();

        auto const* field = r.value();

        if (field->getSType() != STI_ARRAY)
            return HF_ERR_NO_ARRAY;
        int32_t const sz = static_cast<STArray const*>(field)->size();

        return sz;
    }

    Expected<int32_t, HostFuncError>
    updateData(Bytes const& data) override
    {
        ripple::detail::ApplyViewBase v(
            env_.app().openLedger().current().get(), tapNONE);

        auto sle = v.peek(leKey);
        if (!sle)
            return HF_ERR_LEDGER_OBJ_NOT_FOUND;

        sle->setFieldVL(sfData, data);
        v.update(sle);

        return data.size();
    }

    Expected<Hash, HostFuncError>
    computeSha512HalfHash(Bytes const& data) override
    {
        auto const hash = sha512Half(data);
        return hash;
    }

    Expected<Bytes, HostFuncError>
    accountKeylet(AccountID const& account) override
    {
        if (!account)
            return Unexpected(HF_ERR_INVALID_ACCOUNT);
        auto const keylet = keylet::account(account);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFuncError>
    credentialKeylet(
        AccountID const& subject,
        AccountID const& issuer,
        Bytes const& credentialType) override
    {
        if (!subject || !issuer || credentialType.empty() ||
            credentialType.size() > maxCredentialTypeLength)
            return Unexpected(HF_ERR_INVALID_PARAMS);

        auto const keylet =
            keylet::credential(subject, issuer, makeSlice(credentialType));

        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFuncError>
    escrowKeylet(AccountID const& account, std::uint32_t seq) override
    {
        if (!account)
            return Unexpected(HF_ERR_INVALID_ACCOUNT);
        auto const keylet = keylet::escrow(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFuncError>
    oracleKeylet(AccountID const& account, std::uint32_t documentId) override
    {
        if (!account)
            return Unexpected(HF_ERR_INVALID_ACCOUNT);
        auto const keylet = keylet::oracle(account, documentId);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFuncError>
    getNFT(AccountID const& account, uint256 const& nftId) override
    {
        if (!account || !nftId)
        {
            getJournal().trace() << "WAMR getNFT: Invalid account or NFT ID";
            return Unexpected(HF_ERR_INVALID_PARAMS);
        }

        auto obj = nft::findToken(*env_.current(), account, nftId);
        if (!obj)
        {
            getJournal().trace() << "WAMR getNFT: NFT not found";
            return Unexpected(HF_ERR_LEDGER_OBJ_NOT_FOUND);
        }

        auto ouri = obj->at(~sfURI);
        if (!ouri)
            return Bytes();

        Slice const s = ouri->value();
        return Bytes(s.begin(), s.end());
    }

    Expected<int32_t, HostFuncError>
    trace(std::string const& msg, Bytes const& data, bool asHex) override
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        auto j = getJournal().error();
#endif
        if (!asHex)
            j << msg
              << std::string_view(
                     reinterpret_cast<char const*>(data.data()), data.size());
        else
        {
            auto const hex =
                boost::algorithm::hex(std::string(data.begin(), data.end()));
            j << msg << hex;
        }

#ifdef DEBUG_OUTPUT
        j << std::endl;
#endif

        return msg.size() + data.size() * (asHex ? 2 : 1);
    }

    Expected<int32_t, HostFuncError>
    traceNum(std::string const& msg, int64_t data) override
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        auto j = getJournal().error();
#endif
        j << msg << data;

#ifdef DEBUG_OUTPUT
        j << std::endl;
#endif
        return msg.size() + sizeof(data);
    }
};

struct Wasm_test : public beast::unit_test::suite
{
    void
    testWasmFib()
    {
        testcase("Wasm fibo");

        auto const ws = boost::algorithm::unhex(fib32Hex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const re = engine.run(wasm, "fib", wasmParams(10));

        BEAST_EXPECT(re.has_value() && (re->result == 55) && (re->cost == 755));
    }

    void
    testWasmSha()
    {
        testcase("Wasm sha");

        auto const ws = boost::algorithm::unhex(sha512PureHex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const re =
            engine.run(wasm, "sha512_process", wasmParams(sha512PureHex));

        BEAST_EXPECT(
            re.has_value() && (re->result == 34432) && (re->cost == 157'452));
    }

    void
    testWasmB58()
    {
        testcase("Wasm base58");
        auto const ws = boost::algorithm::unhex(b58Hex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        Bytes outb;
        outb.resize(1024);

        auto const minsz = std::min(
            static_cast<std::uint32_t>(512),
            static_cast<std::uint32_t>(b58Hex.size()));
        auto const s = std::string_view(b58Hex.c_str(), minsz);
        auto const re = engine.run(wasm, "b58enco", wasmParams(outb, s));

        BEAST_EXPECT(re.has_value() && re->result && (re->cost == 3'066'129));
    }

    void
    testWasmSP1Verifier()
    {
        testcase("Wasm sp1 zkproof verifier");
        auto const ws = boost::algorithm::unhex(sp1_wasm);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const re = engine.run(wasm, "sp1_groth16_verifier");

        BEAST_EXPECT(
            re.has_value() && re->result && (re->cost == 4'191'711'969ll));
    }

    void
    testWasmBG16Verifier()
    {
        testcase("Wasm BG16 zkproof verifier");
        auto const ws = boost::algorithm::unhex(zkProofHex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const re = engine.run(wasm, "bellman_groth16_test");

        BEAST_EXPECT(re.has_value() && re->result && (re->cost == 332'205'984));
    }

    void
    testWasmLedgerSqn()
    {
        testcase("Wasm get ledger sequence");

        auto wasmStr = boost::algorithm::unhex(ledgerSqnHex);
        Bytes wasm(wasmStr.begin(), wasmStr.end());

        using namespace test::jtx;

        Env env{*this};
        TestLedgerDataProvider ledgerDataProvider(&env);
        std::string const funcName("finish");

        std::vector<WasmImportFunc> imports;
        WASM_IMPORT_FUNC(imports, getLedgerSqn, &ledgerDataProvider, 33);

        auto& engine = WasmEngine::instance();

        auto re = engine.run(
            wasm, funcName, {}, imports, nullptr, 1'000'000, env.journal);

        // code takes 4 gas + 1 getLedgerSqn call
        if (BEAST_EXPECT(re.has_value()))
            BEAST_EXPECT(!re->result && (re->cost == 37));

        env.close();
        env.close();
        env.close();
        env.close();

        // empty module - run the same instance
        re = engine.run(
            {}, funcName, {}, imports, nullptr, 1'000'000, env.journal);

        // code takes 8 gas + 2 getLedgerSqn calls
        if (BEAST_EXPECT(re.has_value()))
            BEAST_EXPECT(re->result && (re->cost == 74));
    }

    void
    testWasmCheckJson()
    {
        testcase("Wasm check json");

        using namespace test::jtx;
        Env env{*this};

        auto const wasmStr = boost::algorithm::unhex(checkJsonHex);
        Bytes const wasm(wasmStr.begin(), wasmStr.end());
        std::string const funcName("check_accountID");
        {
            std::string str = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            Bytes data(str.begin(), str.end());
            auto re = runEscrowWasm(
                wasm, funcName, wasmParams(data), nullptr, -1, env.journal);
            if (BEAST_EXPECT(re.has_value()))
                BEAST_EXPECT(re.value().result && (re->cost == 838));
        }
        {
            std::string str = "rHb9CJAWyB4rj91VRWn96DkukG4bwdty00";
            Bytes data(str.begin(), str.end());
            auto re = runEscrowWasm(
                wasm, funcName, wasmParams(data), nullptr, -1, env.journal);
            if (BEAST_EXPECT(re.has_value()))
                BEAST_EXPECT(!re.value().result && (re->cost == 822));
        }
    }

    void
    testWasmCompareJson()
    {
        testcase("Wasm compare json");

        using namespace test::jtx;
        Env env{*this};

        auto wasmStr = boost::algorithm::unhex(compareJsonHex);
        std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());
        std::string funcName("compare_accountID");

        std::vector<uint8_t> const tx_data(tx_js.begin(), tx_js.end());
        std::vector<uint8_t> const lo_data(lo_js.begin(), lo_js.end());
        auto re = runEscrowWasm(
            wasm,
            funcName,
            wasmParams(tx_data, lo_data),
            nullptr,
            -1,
            env.journal);
        if (BEAST_EXPECT(re.has_value()))
            BEAST_EXPECT(re.value().result && (re->cost == 42'212));

        std::vector<uint8_t> const lo_data2(lo_js2.begin(), lo_js2.end());
        re = runEscrowWasm(
            wasm,
            funcName,
            wasmParams(tx_data, lo_data2),
            nullptr,
            -1,
            env.journal);
        if (BEAST_EXPECT(re.has_value()))
            BEAST_EXPECT(!re.value().result && (re->cost == 41'496));
    }

    void
    testWasmLib()
    {
        testcase("wasmtime lib test");
        // clang-format off
        /* The WASM module buffer. */
        Bytes const wasm = {/* WASM header */
                          0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,
                          /* Type section */
                          0x01, 0x07, 0x01,
                          /* function type {i32, i32} -> {i32} */
                          0x60, 0x02, 0x7F, 0x7F, 0x01, 0x7F,
                          /* Import section */
                          0x02, 0x13, 0x01,
                          /* module name: "extern" */
                          0x06, 0x65, 0x78, 0x74, 0x65, 0x72, 0x6E,
                          /* extern name: "func-add" */
                          0x08, 0x66, 0x75, 0x6E, 0x63, 0x2D, 0x61, 0x64, 0x64,
                          /* import desc: func 0 */
                          0x00, 0x00,
                          /* Function section */
                          0x03, 0x02, 0x01, 0x00,
                          /* Export section */
                          0x07, 0x0A, 0x01,
                          /* export name: "addTwo" */
                          0x06, 0x61, 0x64, 0x64, 0x54, 0x77, 0x6F,
                          /* export desc: func 0 */
                          0x00, 0x01,
                          /* Code section */
                          0x0A, 0x0A, 0x01,
                          /* code body */
                          0x08, 0x00, 0x20, 0x00, 0x20, 0x01, 0x10, 0x00, 0x0B};
        // clang-format on
        auto& vm = WasmEngine::instance();

        std::vector<WasmImportFunc> imports;
        WasmImpFunc<Add_proto>(
            imports, "func-add", reinterpret_cast<void*>(&Add));

        auto re = vm.run(wasm, "addTwo", wasmParams(1234, 5678), imports);

        // if (res) printf("invokeAdd get the result: %d\n", res.value());

        BEAST_EXPECT(re.has_value() && re->result == 6912 && (re->cost == 2));
    }

    void
    testBadWasm()
    {
        testcase("bad wasm test");

        using namespace test::jtx;

        Env env{*this};
        HostFunctions hfs;

        {
            auto wasmHex = "00000000";
            auto wasmStr = boost::algorithm::unhex(std::string(wasmHex));
            std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());
            std::string funcName("mock_escrow");

            auto re = runEscrowWasm(wasm, funcName, {}, &hfs, 15, env.journal);
            BEAST_EXPECT(!re);
        }

        {
            auto wasmHex = "00112233445566778899AA";
            auto wasmStr = boost::algorithm::unhex(std::string(wasmHex));
            std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());
            std::string funcName("mock_escrow");

            auto const re =
                preflightEscrowWasm(wasm, funcName, {}, &hfs, env.journal);
            BEAST_EXPECT(!isTesSuccess(re));
        }

        {
            // FinishFunction wrong function name
            // pub fn bad() -> bool {
            //     unsafe { host_lib::getLedgerSqn() >= 5 }
            // }
            auto const badWasmHex =
                "0061736d010000000105016000017f02190108686f73745f6c69620c6765"
                "744c656467657253716e00000302010005030100100611027f00418080c0"
                "000b7f00418080c0000b072b04066d656d6f727902000362616400010a5f"
                "5f646174615f656e6403000b5f5f686561705f6261736503010a09010700"
                "100041044a0b004d0970726f64756365727302086c616e67756167650104"
                "52757374000c70726f6365737365642d6279010572757374631d312e3835"
                "2e31202834656231363132353020323032352d30332d31352900490f7461"
                "726765745f6665617475726573042b0f6d757461626c652d676c6f62616c"
                "732b087369676e2d6578742b0f7265666572656e63652d74797065732b0a"
                "6d756c746976616c7565";
            auto wasmStr = boost::algorithm::unhex(std::string(badWasmHex));
            std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());
            std::string funcName("finish");

            auto const re =
                preflightEscrowWasm(wasm, funcName, {}, &hfs, env.journal);
            BEAST_EXPECT(!isTesSuccess(re));
        }
    }

    void
    testEscrowWasmDN1()
    {
        testcase("escrow wasm devnet 1 test");
        std::string const wasmHex = allHostFunctionsHex;

        std::string const wasmStr = boost::algorithm::unhex(wasmHex);
        std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());

        //        let sender = get_tx_account_id();
        //        let owner = get_current_escrow_account_id();
        //        let dest = get_current_escrow_destination();
        //        let dest_balance = get_account_balance(dest);
        //        let escrow_data = get_current_escrow_data();
        //        let ed_str = String::from_utf8(escrow_data).unwrap();
        //        let threshold_balance = ed_str.parse::<u64>().unwrap();
        //        let pl_time = host_lib::getParentLedgerTime();
        //        let e_time = get_current_escrow_finish_after();
        //        sender == owner && dest_balance <= threshold_balance &&
        //        pl_time >= e_time

        using namespace test::jtx;
        Env env{*this};
        {
            TestHostFunctions nfs(env, 0);
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 100'000);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(!re->result && (re->cost == 487));
                // std::cout << "good case result " << re.value().result
                //           << " cost: " << re.value().cost << std::endl;
            }
        }

        env.close();
        env.close();
        env.close();
        env.close();

        {  // fail because current time < escrow_finish_after time
            TestHostFunctions nfs(env, -1);
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 100000);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(!re->result && (re->cost == 487));
                // std::cout << "bad case (current time < escrow_finish_after "
                //              "time) result "
                //           << re.value().result << " cost: " <<
                //           re.value().cost
                //           << std::endl;
            }
        }

        {  // fail because trying to access nonexistent field
            struct BadTestHostFunctions : public TestHostFunctions
            {
                explicit BadTestHostFunctions(Env& env) : TestHostFunctions(env)
                {
                }
                Expected<Bytes, HostFuncError>
                getTxField(SField const& fname) override
                {
                    return Unexpected(HF_ERR_FIELD_NOT_FOUND);
                }
            };
            BadTestHostFunctions nfs(env);
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 100000);
            BEAST_EXPECT(re.has_value() && !re->result && (re->cost == 23));
            // std::cout << "bad case (access nonexistent field) result "
            //           << re.error() << std::endl;
        }

        {  // fail because trying to allocate more than MAX_PAGES memory
            struct BadTestHostFunctions : public TestHostFunctions
            {
                explicit BadTestHostFunctions(Env& env) : TestHostFunctions(env)
                {
                }
                Expected<Bytes, HostFuncError>
                getTxField(SField const& fname) override
                {
                    return Bytes((MAX_PAGES + 1) * 64 * 1024, 1);
                }
            };
            BadTestHostFunctions nfs(env);
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 100000);
            BEAST_EXPECT(re.has_value() && !re->result && (re->cost == 23));
            // std::cout << "bad case (more than MAX_PAGES) result "
            //           << re.error() << std::endl;
        }

        {  // fail because recursion too deep
            auto wasmHex = deepRecursionHex;
            auto wasmStr = boost::algorithm::unhex(std::string(wasmHex));
            std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());

            TestHostFunctionsSink nfs(env);
            std::string funcName("recursive");
            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 1000'000'000);
            BEAST_EXPECT(!re && re.error());
            // std::cout << "bad case (deep recursion) result " << re.error()
            //             << std::endl;

            auto const& sink = nfs.getSink();
            auto countSubstr = [](std::string const& str,
                                  std::string const& substr) {
                std::size_t pos = 0;
                int occurrences = 0;
                while ((pos = str.find(substr, pos)) != std::string::npos)
                {
                    occurrences++;
                    pos += substr.length();
                }
                return occurrences;
            };

            auto const s = sink.messages().str();
            BEAST_EXPECT(
                countSubstr(s, "WAMR Error: failed to call func") == 1);
            BEAST_EXPECT(
                countSubstr(s, "Exception: wasm operand stack overflow") > 0);
        }

        {
            auto wasmStr = boost::algorithm::unhex(ledgerSqnHex);
            Bytes wasm(wasmStr.begin(), wasmStr.end());
            std::string const funcName("finish");
            TestLedgerDataProvider ledgerDataProvider(&env);

            std::vector<WasmImportFunc> imports;
            WASM_IMPORT_FUNC2(
                imports, getLedgerSqn, "get_ledger_sqn2", &ledgerDataProvider);

            auto& engine = WasmEngine::instance();

            auto re = engine.run(
                wasm, funcName, {}, imports, nullptr, 1'000'000, env.journal);

            // expected import not provided
            BEAST_EXPECT(!re);
        }
    }

    void
    testEscrowWasmDN2()
    {
        testcase("wasm devnet 3 test");

        std::string const funcName("finish");

        using namespace test::jtx;

        Env env(*this);
        {
            std::string const wasmHex = xrplStdExampleHex;
            std::string const wasmStr = boost::algorithm::unhex(wasmHex);
            std::vector<uint8_t> const wasm(wasmStr.begin(), wasmStr.end());

            TestHostFunctions nfs(env, 0);

            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 100'000);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re->result && (re->cost == 80));
                // std::cout << "good case result " << re.value().result
                //           << " cost: " << re.value().cost << std::endl;
            }
        }

        env.close();
        env.close();
        env.close();
        env.close();
        env.close();

        {
            std::string const wasmHex = hostFunctions2Hex;
            std::string const wasmStr = boost::algorithm::unhex(wasmHex);
            std::vector<uint8_t> const wasm(wasmStr.begin(), wasmStr.end());

            TestHostFunctions nfs(env, 0);

            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 100'000);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re->result && (re->cost == 138));
                // std::cout << "good case result " << re.value().result
                //           << " cost: " << re.value().cost << std::endl;
            }
        }
    }

    void
    perfTest()
    {
        testcase("Perf test host functions");

        using namespace jtx;
        using namespace std::chrono;

        std::string const funcName("finish");
        // std::string const funcName("test");
        auto const& wasmHex = hfPerfTest;
        // auto const& wasmHex = opcCallPerfTest;
        std::string const wasmStr = boost::algorithm::unhex(wasmHex);
        std::vector<uint8_t> const wasm(wasmStr.begin(), wasmStr.end());

        std::string const credType = "abcde";
        std::string const credType2 = "fghijk";
        std::string const credType3 = "0123456";
        // char const uri[] = "uri";

        Account const alan{"alan"};
        Account const bob{"bob"};
        Account const issuer{"issuer"};

        {
            Env env(*this);
            // Env env(*this, envconfig(), {}, nullptr,
            // beast::severities::kTrace);
            env.fund(XRP(5000), alan, bob, issuer);
            env.close();

            // // create escrow
            // auto const seq = env.seq(alan);
            // auto const k = keylet::escrow(alan, seq);
            // // auto const allowance = 3'600;
            // auto escrowCreate = escrow::create(alan, bob, XRP(1000));
            // XRPAmount txnFees = env.current()->fees().base + 1000;
            // env(escrowCreate,
            //     escrow::finish_function(wasmHex),
            //     escrow::finish_time(env.now() + 11s),
            //     escrow::cancel_time(env.now() + 100s),
            //     escrow::data("1000000000"),  // 1000 XRP in drops
            //     memodata("memo1234567"),
            //     memodata("2memo1234567"),
            //     fee(txnFees));

            // // create depositPreauth
            // auto const k = keylet::depositPreauth(
            //     bob,
            //     {{issuer.id(), makeSlice(credType)},
            //      {issuer.id(), makeSlice(credType2)},
            //      {issuer.id(), makeSlice(credType3)}});
            // env(deposit::authCredentials(
            //     bob,
            //     {{issuer, credType},
            //      {issuer, credType2},
            //      {issuer, credType3}}));

            // cREATE nft
            [[maybe_unused]] uint256 const nft0{
                token::getNextID(env, alan, 0u)};
            env(token::mint(alan, 0u));
            auto const k = keylet::nftoffer(alan, 0);
            [[maybe_unused]] uint256 const nft1{
                token::getNextID(env, alan, 0u)};

            env(token::mint(alan, 0u),
                token::uri(
                    "https://github.com/XRPLF/XRPL-Standards/discussions/"
                    "279?id=github.com/XRPLF/XRPL-Standards/discussions/"
                    "279&ut=github.com/XRPLF/XRPL-Standards/discussions/"
                    "279&sid=github.com/XRPLF/XRPL-Standards/discussions/"
                    "279&aot=github.com/XRPLF/XRPL-Standards/disc"));
            [[maybe_unused]] uint256 const nft2{
                token::getNextID(env, alan, 0u)};
            env(token::mint(alan, 0u));
            env.close();

            PerfHostFunctions nfs(env, k, env.tx());

            auto re = runEscrowWasm(wasm, funcName, {}, &nfs);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re->result);
                std::cout << "Res: " << re->result << " cost: " << re->cost
                          << std::endl;
            }

            // env(escrow::finish(alan, alan, seq),
            //     escrow::comp_allowance(allowance),
            //     fee(txnFees),
            //     ter(tesSUCCESS));

            env.close();
        }
    }

    void
    run() override
    {
        using namespace test::jtx;

        testWasmLib();
        testBadWasm();
        testWasmCheckJson();
        testWasmCompareJson();
        testWasmLedgerSqn();

        testWasmFib();
        testWasmSha();
        testWasmB58();

        // runing too long
        // testWasmSP1Verifier();
        testWasmBG16Verifier();

        // TODO: fix result
        testEscrowWasmDN1();
        testEscrowWasmDN2();

        // perfTest();
    }
};

BEAST_DEFINE_TESTSUITE(Wasm, app, ripple);

}  // namespace test
}  // namespace ripple
