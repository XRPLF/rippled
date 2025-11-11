#include <test/app/wasm_fixtures/fixtures.h>
#include <test/jtx.h>

#include <xrpld/app/misc/AmendmentTable.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/app/wasm/HostFunc.h>
#include <xrpld/app/wasm/WasmVM.h>
#include <xrpl/protocol/digest.h>

#include <xrpl/ledger/detail/ApplyViewBase.h>
#include <xrpl/protocol/digest.h>

namespace ripple {

namespace test {

struct TestLedgerDataProvider : public HostFunctions
{
    jtx::Env* env_;
    void const* rt_ = nullptr;

public:
    TestLedgerDataProvider(jtx::Env* env) : env_(env)
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

    Expected<std::int32_t, HostFunctionError>
    getLedgerSqn() override
    {
        return env_->current()->seq();
    }
};

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
        accountID_ = env_.master.id();
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

    Expected<std::int32_t, HostFunctionError>
    getLedgerSqn() override
    {
        return 12345;
    }

    Expected<std::int32_t, HostFunctionError>
    getParentLedgerTime() override
    {
        return 67890;
    }

    Expected<Hash, HostFunctionError>
    getParentLedgerHash() override
    {
        return env_.current()->info().parentHash;
    }

    Expected<int32_t, HostFunctionError>
    getBaseFee() override
    {
        return 10;
    }

    Expected<int32_t, HostFunctionError>
    isAmendmentEnabled(uint256 const& amendmentId) override
    {
        return 1;
    }

    Expected<int32_t, HostFunctionError>
    isAmendmentEnabled(std::string_view const& amendmentName) override
    {
        return 1;
    }

    virtual Expected<int32_t, HostFunctionError>
    cacheLedgerObj(uint256 const& objId, int32_t cacheIdx) override
    {
        return 1;
    }

    Expected<Bytes, HostFunctionError>
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
            std::uint32_t const data = x.value();
            auto const* b = reinterpret_cast<uint8_t const*>(&data);
            auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
            return Bytes{b, e};
        }
        return Bytes();
    }

    Expected<Bytes, HostFunctionError>
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

        return Unexpected(HostFunctionError::INTERNAL);
    }

    Expected<Bytes, HostFunctionError>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) override
    {
        if (fname == sfBalance)
        {
            int64_t x = 10'000;
            uint8_t const* p = reinterpret_cast<uint8_t const*>(&x);
            return Bytes{p, p + sizeof(x)};
        }
        else if (fname == sfAccount)
        {
            return Bytes(accountID_.begin(), accountID_.end());
        }
        return data_;
    }

    Expected<Bytes, HostFunctionError>
    getTxNestedField(Slice const& locator) override
    {
        if (locator.size() == 4)
        {
            int32_t const* l = reinterpret_cast<int32_t const*>(locator.data());
            int32_t const sfield = l[0];
            if (sfield == sfAccount.fieldCode)
            {
                return Bytes(accountID_.begin(), accountID_.end());
            }
        }
        uint8_t const a[] = {0x2b, 0x6a, 0x23, 0x2a, 0xa4, 0xc4, 0xbe, 0x41,
                             0xbf, 0x49, 0xd2, 0x45, 0x9f, 0xa4, 0xa0, 0x34,
                             0x7e, 0x1b, 0x54, 0x3a, 0x4c, 0x92, 0xfc, 0xee,
                             0x08, 0x21, 0xc0, 0x20, 0x1e, 0x2e, 0x9a, 0x00};
        return Bytes(&a[0], &a[sizeof(a)]);
    }

    Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjNestedField(Slice const& locator) override
    {
        if (locator.size() == 4)
        {
            int32_t const* l = reinterpret_cast<int32_t const*>(locator.data());
            int32_t const sfield = l[0];
            if (sfield == sfAccount.fieldCode)
            {
                return Bytes(accountID_.begin(), accountID_.end());
            }
        }
        uint8_t const a[] = {0x2b, 0x6a, 0x23, 0x2a, 0xa4, 0xc4, 0xbe, 0x41,
                             0xbf, 0x49, 0xd2, 0x45, 0x9f, 0xa4, 0xa0, 0x34,
                             0x7e, 0x1b, 0x54, 0x3a, 0x4c, 0x92, 0xfc, 0xee,
                             0x08, 0x21, 0xc0, 0x20, 0x1e, 0x2e, 0x9a, 0x00};
        return Bytes(&a[0], &a[sizeof(a)]);
    }

    Expected<Bytes, HostFunctionError>
    getLedgerObjNestedField(int32_t cacheIdx, Slice const& locator) override
    {
        if (locator.size() == 4)
        {
            int32_t const* l = reinterpret_cast<int32_t const*>(locator.data());
            int32_t const sfield = l[0];
            if (sfield == sfAccount.fieldCode)
            {
                return Bytes(accountID_.begin(), accountID_.end());
            }
        }
        uint8_t const a[] = {0x2b, 0x6a, 0x23, 0x2a, 0xa4, 0xc4, 0xbe, 0x41,
                             0xbf, 0x49, 0xd2, 0x45, 0x9f, 0xa4, 0xa0, 0x34,
                             0x7e, 0x1b, 0x54, 0x3a, 0x4c, 0x92, 0xfc, 0xee,
                             0x08, 0x21, 0xc0, 0x20, 0x1e, 0x2e, 0x9a, 0x00};
        return Bytes(&a[0], &a[sizeof(a)]);
    }

    Expected<int32_t, HostFunctionError>
    getTxArrayLen(SField const& fname) override
    {
        return 32;
    }

    Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjArrayLen(SField const& fname) override
    {
        return 32;
    }

    Expected<int32_t, HostFunctionError>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname) override
    {
        return 32;
    }

    Expected<int32_t, HostFunctionError>
    getTxNestedArrayLen(Slice const& locator) override
    {
        return 32;
    }

    Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjNestedArrayLen(Slice const& locator) override
    {
        return 32;
    }

    Expected<int32_t, HostFunctionError>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, Slice const& locator) override
    {
        return 32;
    }

    Expected<int32_t, HostFunctionError>
    updateData(Slice const& data) override
    {
        return data.size();
    }

    Expected<int32_t, HostFunctionError>
    checkSignature(
        Slice const& message,
        Slice const& signature,
        Slice const& pubkey) override
    {
        return 1;
    }

    Expected<Hash, HostFunctionError>
    computeSha512HalfHash(Slice const& data) override
    {
        return env_.current()->info().parentHash;
    }

    Expected<Bytes, HostFunctionError>
    accountKeylet(AccountID const& account) override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::account(account);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    ammKeylet(Asset const& issue1, Asset const& issue2) override
    {
        if (issue1 == issue2)
            return Unexpected(HostFunctionError::INVALID_PARAMS);
        if (issue1.holds<MPTIssue>() || issue2.holds<MPTIssue>())
            return Unexpected(HostFunctionError::INVALID_PARAMS);
        auto const keylet = keylet::amm(issue1, issue2);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    credentialKeylet(
        AccountID const& subject,
        AccountID const& issuer,
        Slice const& credentialType) override
    {
        if (!subject || !issuer || credentialType.empty() ||
            credentialType.size() > maxCredentialTypeLength)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::credential(subject, issuer, credentialType);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    escrowKeylet(AccountID const& account, std::uint32_t seq) override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::escrow(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    oracleKeylet(AccountID const& account, std::uint32_t documentId) override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::oracle(account, documentId);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    getNFT(AccountID const& account, uint256 const& nftId) override
    {
        if (!account || !nftId)
        {
            return Unexpected(HostFunctionError::INVALID_PARAMS);
        }

        std::string s = "https://ripple.com";
        return Bytes(s.begin(), s.end());
    }

    Expected<Bytes, HostFunctionError>
    getNFTIssuer(uint256 const& nftId) override
    {
        return Bytes(accountID_.begin(), accountID_.end());
    }

    Expected<std::uint32_t, HostFunctionError>
    getNFTTaxon(uint256 const& nftId) override
    {
        return 4;
    }

    Expected<int32_t, HostFunctionError>
    getNFTFlags(uint256 const& nftId) override
    {
        return 8;
    }

    Expected<int32_t, HostFunctionError>
    getNFTTransferFee(uint256 const& nftId) override
    {
        return 10;
    }

    Expected<std::uint32_t, HostFunctionError>
    getNFTSerial(uint256 const& nftId) override
    {
        return 4;
    }

    Expected<int32_t, HostFunctionError>
    trace(std::string_view const& msg, Slice const& data, bool asHex) override
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        auto j = getJournal().trace();
#endif
        if (!asHex)
        {
            j << "WASM TRACE: " << msg << " "
              << std::string_view(
                     reinterpret_cast<char const*>(data.data()), data.size());
        }
        else
        {
            std::string hex;
            hex.reserve(data.size() * 2);
            boost::algorithm::hex(
                data.begin(), data.end(), std::back_inserter(hex));
            j << "WASM DEV TRACE: " << msg << " " << hex;
        }

#ifdef DEBUG_OUTPUT
        j << std::endl;
#endif

        return msg.size() + data.size() * (asHex ? 2 : 1);
    }

    Expected<int32_t, HostFunctionError>
    traceNum(std::string_view const& msg, int64_t data) override
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        auto j = getJournal().trace();
#endif
        j << "WASM TRACE NUM: " << msg << " " << data;

#ifdef DEBUG_OUTPUT
        j << std::endl;
#endif
        return msg.size() + sizeof(data);
    }

    Expected<int32_t, HostFunctionError>
    traceAccount(std::string_view const& msg, AccountID const& account) override
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        auto j = getJournal().trace();
#endif
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);

        auto const accountStr = toBase58(account);

        j << "WASM TRACE ACCOUNT: " << msg << " " << accountStr;
        return msg.size() + accountStr.size();
    }

    Expected<int32_t, HostFunctionError>
    traceFloat(std::string_view const& msg, Slice const& data) override
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        auto j = getJournal().trace();
#endif
        auto const s = floatToString(data);
        j << "WASM TRACE FLOAT: " << msg << " " << s;

#ifdef DEBUG_OUTPUT
        j << std::endl;
#endif
        return msg.size() + s.size();
    }

    Expected<int32_t, HostFunctionError>
    traceAmount(std::string_view const& msg, STAmount const& amount) override
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        auto j = getJournal().trace();
#endif
        auto const amountStr = amount.getFullText();
        j << "WASM TRACE AMOUNT: " << msg << " " << amountStr;
        return msg.size() + amountStr.size();
    }

    Expected<Bytes, HostFunctionError>
    floatFromInt(int64_t x, int32_t mode) override
    {
        return floatFromIntImpl(x, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatFromUint(uint64_t x, int32_t mode) override
    {
        return floatFromUintImpl(x, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatSet(int64_t mantissa, int32_t exponent, int32_t mode) override
    {
        return floatSetImpl(mantissa, exponent, mode);
    }

    Expected<int32_t, HostFunctionError>
    floatCompare(Slice const& x, Slice const& y) override
    {
        return floatCompareImpl(x, y);
    }

    Expected<Bytes, HostFunctionError>
    floatAdd(Slice const& x, Slice const& y, int32_t mode) override
    {
        return floatAddImpl(x, y, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatSubtract(Slice const& x, Slice const& y, int32_t mode) override
    {
        return floatSubtractImpl(x, y, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatMultiply(Slice const& x, Slice const& y, int32_t mode) override
    {
        return floatMultiplyImpl(x, y, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatDivide(Slice const& x, Slice const& y, int32_t mode) override
    {
        return floatDivideImpl(x, y, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatRoot(Slice const& x, int32_t n, int32_t mode) override
    {
        return floatRootImpl(x, n, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatPower(Slice const& x, int32_t n, int32_t mode) override
    {
        return floatPowerImpl(x, n, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatLog(Slice const& x, int32_t mode) override
    {
        return floatLogImpl(x, mode);
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

struct PerfHostFunctions : public TestHostFunctions
{
    Keylet leKey;
    std::shared_ptr<SLE const> currentLedgerObj = nullptr;
    bool isLedgerObjCached = false;

    static int constexpr MAX_CACHE = 256;
    std::array<std::shared_ptr<SLE const>, MAX_CACHE> cache;
    // std::optional<Bytes> data_; // deferred data update, not used in
    // performance
    std::shared_ptr<STTx const> tx_;

    void const* rt_ = nullptr;

    PerfHostFunctions(
        test::jtx::Env& env,
        Keylet const& k,
        std::shared_ptr<STTx const>&& tx)
        : TestHostFunctions(env), leKey(k), tx_(std::move(tx))
    {
    }

    Expected<std::int32_t, HostFunctionError>
    getLedgerSqn() override
    {
        auto seq = env_.current()->seq();
        if (seq > std::numeric_limits<int32_t>::max())
            return Unexpected(HostFunctionError::INTERNAL);  // LCOV_EXCL_LINE
        return static_cast<int32_t>(seq);
    }

    Expected<std::int32_t, HostFunctionError>
    getParentLedgerTime() override
    {
        auto time =
            env_.current()->parentCloseTime().time_since_epoch().count();
        if (time > std::numeric_limits<int32_t>::max())
            return Unexpected(HostFunctionError::INTERNAL);
        return static_cast<int32_t>(time);
    }

    Expected<Hash, HostFunctionError>
    getParentLedgerHash() override
    {
        return env_.current()->info().parentHash;
    }

    Expected<int32_t, HostFunctionError>
    getBaseFee() override
    {
        auto fee = env_.current()->fees().base.drops();
        if (fee > std::numeric_limits<int32_t>::max())
            return Unexpected(HostFunctionError::INTERNAL);
        return static_cast<int32_t>(fee);
    }

    Expected<int32_t, HostFunctionError>
    isAmendmentEnabled(uint256 const& amendmentId) override
    {
        return env_.current()->rules().enabled(amendmentId);
    }

    Expected<int32_t, HostFunctionError>
    isAmendmentEnabled(std::string_view const& amendmentName) override
    {
        auto const& table = env_.app().getAmendmentTable();
        auto const amendment = table.find(std::string(amendmentName));
        return env_.current()->rules().enabled(amendment);
    }

    Expected<std::shared_ptr<SLE const>, HostFunctionError>
    getCurrentLedgerObj()
    {
        if (!isLedgerObjCached)
        {
            isLedgerObjCached = true;
            currentLedgerObj = env_.le(leKey);
        }
        if (currentLedgerObj)
            return currentLedgerObj;
        return Unexpected(HostFunctionError::LEDGER_OBJ_NOT_FOUND);
    }

    Expected<std::shared_ptr<SLE const>, HostFunctionError>
    peekCurrentLedgerObj(int32_t cacheIdx)
    {
        --cacheIdx;
        if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
            return Unexpected(HostFunctionError::SLOT_OUT_RANGE);

        if (!cache[cacheIdx])
        {  // return Unexpected(HostFunctionError::INVALID_SLOT);
            auto const r = getCurrentLedgerObj();
            if (!r)
                return Unexpected(r.error());
            cache[cacheIdx] = *r;
        }

        return cache[cacheIdx];
    }

    Expected<int32_t, HostFunctionError>
    normalizeCacheIndex(int32_t cacheIdx)
    {
        --cacheIdx;
        if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
            return Unexpected(HostFunctionError::SLOT_OUT_RANGE);
        if (!cache[cacheIdx])
            return Unexpected(HostFunctionError::EMPTY_SLOT);
        return cacheIdx;
    }

    virtual Expected<int32_t, HostFunctionError>
    cacheLedgerObj(uint256 const&, int32_t cacheIdx) override
    {
        // auto const& keylet = keylet::unchecked(objId);

        static int32_t intIdx = 0;

        if (cacheIdx < 0 || cacheIdx > MAX_CACHE)
            return Unexpected(HostFunctionError::SLOT_OUT_RANGE);

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
            return Unexpected(HostFunctionError::SLOTS_FULL);

        cache[cacheIdx] = env_.le(leKey);
        if (!cache[cacheIdx])
            return Unexpected(HostFunctionError::LEDGER_OBJ_NOT_FOUND);
        return cacheIdx + 1;
    }

    static Expected<Bytes, HostFunctionError>
    getAnyFieldData(STBase const* obj)
    {
        // auto const& fname = obj.getFName();
        if (!obj)
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

        auto const stype = obj->getSType();
        switch (stype)
        {
            // LCOV_EXCL_START
            case STI_UNKNOWN:
            case STI_NOTPRESENT:
                return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
                break;
            // LCOV_EXCL_STOP
            case STI_OBJECT:
            case STI_ARRAY:
                return Unexpected(HostFunctionError::NOT_LEAF_FIELD);
                break;
            case STI_ACCOUNT: {
                auto const* account(static_cast<STAccount const*>(obj));
                auto const& data = account->value();
                return Bytes{data.begin(), data.end()};
            }
            break;
            case STI_AMOUNT:
                // will be processed by serializer
                break;
            case STI_ISSUE: {
                auto const* issue(static_cast<STIssue const*>(obj));
                Asset const& asset(issue->value());
                // XRP and IOU will be processed by serializer
                if (asset.holds<MPTIssue>())
                {
                    // MPT
                    auto const& mptIssue = asset.get<MPTIssue>();
                    auto const& mptID = mptIssue.getMptID();
                    return Bytes{mptID.cbegin(), mptID.cend()};
                }
            }
            break;
            case STI_VL: {
                auto const* vl(static_cast<STBlob const*>(obj));
                auto const& data = vl->value();
                return Bytes{data.begin(), data.end()};
            }
            break;
            case STI_UINT16: {
                auto const& num(
                    static_cast<STInteger<std::uint16_t> const*>(obj));
                std::uint16_t const data = num->value();
                auto const* b = reinterpret_cast<uint8_t const*>(&data);
                auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
                return Bytes{b, e};
            }
            case STI_UINT32: {
                auto const* num(
                    static_cast<STInteger<std::uint32_t> const*>(obj));
                std::uint32_t const data = num->value();
                auto const* b = reinterpret_cast<uint8_t const*>(&data);
                auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
                return Bytes{b, e};
            }
            break;
            default:
                break;  // default to serializer
        }

        Serializer msg;
        obj->add(msg);
        auto const data = msg.getData();

        return data;
    }

    Expected<Bytes, HostFunctionError>
    getTxField(SField const& fname) override
    {
        return getAnyFieldData(tx_->peekAtPField(fname));
    }

    Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjField(SField const& fname) override
    {
        auto const sle = getCurrentLedgerObj();
        if (!sle)
            return Unexpected(sle.error());
        return getAnyFieldData((*sle)->peekAtPField(fname));
    }

    Expected<Bytes, HostFunctionError>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) override
    {
        auto const sle = peekCurrentLedgerObj(cacheIdx);
        if (!sle)
            return Unexpected(sle.error());
        return getAnyFieldData((*sle)->peekAtPField(fname));
    }

    static inline bool
    noField(STBase const* field)
    {
        return !field || (STI_NOTPRESENT == field->getSType()) ||
            (STI_UNKNOWN == field->getSType());
    }

    static Expected<STBase const*, HostFunctionError>
    locateField(STObject const& obj, Slice const& locator)
    {
        if (locator.empty() || (locator.size() & 3))  // must be multiple of 4
            return Unexpected(HostFunctionError::LOCATOR_MALFORMED);

        int32_t const* locPtr =
            reinterpret_cast<int32_t const*>(locator.data());
        int32_t const locSize = locator.size() / 4;
        STBase const* field = nullptr;
        auto const& knownSFields = SField::getKnownCodeToField();

        {
            int32_t const sfieldCode = locPtr[0];
            auto const it = knownSFields.find(sfieldCode);
            if (it == knownSFields.end())
                return Unexpected(HostFunctionError::INVALID_FIELD);

            auto const& fname(*it->second);
            field = obj.peekAtPField(fname);
            if (noField(field))
                return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
        }

        for (int i = 1; i < locSize; ++i)
        {
            int32_t const sfieldCode = locPtr[i];

            if (STI_ARRAY == field->getSType())
            {
                auto const* arr = static_cast<STArray const*>(field);
                if (sfieldCode >= arr->size())
                    return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);
                field = &(arr->operator[](sfieldCode));
            }
            else if (STI_OBJECT == field->getSType())
            {
                auto const* o = static_cast<STObject const*>(field);

                auto const it = knownSFields.find(sfieldCode);
                if (it == knownSFields.end())
                    return Unexpected(HostFunctionError::INVALID_FIELD);

                auto const& fname(*it->second);
                field = o->peekAtPField(fname);
            }
            else  // simple field must be the last one
            {
                return Unexpected(HostFunctionError::LOCATOR_MALFORMED);
            }

            if (noField(field))
                return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
        }

        return field;
    }

    Expected<Bytes, HostFunctionError>
    getTxNestedField(Slice const& locator) override
    {
        // std::cout << tx_->getJson(JsonOptions::none).toStyledString() <<
        // std::endl;
        auto const r = locateField(*tx_, locator);
        if (!r)
            return Unexpected(r.error());
        return getAnyFieldData(*r);
    }

    Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjNestedField(Slice const& locator) override
    {
        auto const sle = getCurrentLedgerObj();
        if (!sle)
            return Unexpected(sle.error());

        auto const r = locateField(**sle, locator);
        if (!r)
            return Unexpected(r.error());

        return getAnyFieldData(*r);
    }

    Expected<Bytes, HostFunctionError>
    getLedgerObjNestedField(int32_t cacheIdx, Slice const& locator) override
    {
        auto const sle = peekCurrentLedgerObj(cacheIdx);
        if (!sle)
            return Unexpected(sle.error());

        auto const r = locateField(**sle, locator);
        if (!r)
            return Unexpected(r.error());

        return getAnyFieldData(*r);
    }

    Expected<int32_t, HostFunctionError>
    getTxArrayLen(SField const& fname) override
    {
        if (fname.fieldType != STI_ARRAY)
            return Unexpected(HostFunctionError::NO_ARRAY);

        auto const* field = tx_->peekAtPField(fname);
        if (noField(field))
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

        if (field->getSType() != STI_ARRAY)
            return Unexpected(HostFunctionError::NO_ARRAY);
        int32_t const sz = static_cast<STArray const*>(field)->size();

        return sz;
    }

    Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjArrayLen(SField const& fname) override
    {
        if (fname.fieldType != STI_ARRAY)
            return Unexpected(HostFunctionError::NO_ARRAY);

        auto const sle = getCurrentLedgerObj();
        if (!sle)
            return Unexpected(sle.error());

        auto const* field = (*sle)->peekAtPField(fname);
        if (noField(field))
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

        if (field->getSType() != STI_ARRAY)
            return Unexpected(HostFunctionError::NO_ARRAY);
        int32_t const sz = static_cast<STArray const*>(field)->size();

        return sz;
    }

    Expected<int32_t, HostFunctionError>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname) override
    {
        if (fname.fieldType != STI_ARRAY)
            return Unexpected(HostFunctionError::NO_ARRAY);

        auto const sle = peekCurrentLedgerObj(cacheIdx);
        if (!sle)
            return Unexpected(sle.error());

        auto const* field = (*sle)->peekAtPField(fname);
        if (noField(field))
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

        if (field->getSType() != STI_ARRAY)
            return Unexpected(HostFunctionError::NO_ARRAY);
        int32_t const sz = static_cast<STArray const*>(field)->size();

        return sz;
    }

    Expected<int32_t, HostFunctionError>
    getTxNestedArrayLen(Slice const& locator) override
    {
        auto const r = locateField(*tx_, locator);
        if (!r)
            return Unexpected(r.error());
        auto const* field = r.value();

        if (field->getSType() != STI_ARRAY)
            return Unexpected(HostFunctionError::NO_ARRAY);
        int32_t const sz = static_cast<STArray const*>(field)->size();

        return sz;
    }

    Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjNestedArrayLen(Slice const& locator) override
    {
        auto const sle = getCurrentLedgerObj();
        if (!sle)
            return Unexpected(sle.error());

        auto const r = locateField(**sle, locator);
        if (!r)
            return Unexpected(r.error());
        auto const* field = r.value();

        if (field->getSType() != STI_ARRAY)
            return Unexpected(HostFunctionError::NO_ARRAY);
        int32_t const sz = static_cast<STArray const*>(field)->size();

        return sz;
    }

    Expected<int32_t, HostFunctionError>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, Slice const& locator) override
    {
        auto const sle = peekCurrentLedgerObj(cacheIdx);
        if (!sle)
            return Unexpected(sle.error());

        auto const r = locateField(**sle, locator);
        if (!r)
            return Unexpected(r.error());

        auto const* field = r.value();

        if (field->getSType() != STI_ARRAY)
            return Unexpected(HostFunctionError::NO_ARRAY);
        int32_t const sz = static_cast<STArray const*>(field)->size();

        return sz;
    }

    Expected<int32_t, HostFunctionError>
    updateData(Slice const& data) override
    {
        if (data.size() > maxWasmDataLength)
            return Unexpected(HostFunctionError::DATA_FIELD_TOO_LARGE);

        ripple::detail::ApplyViewBase v(
            env_.app().openLedger().current().get(), tapNONE);

        auto sle = v.peek(leKey);
        if (!sle)
            return Unexpected(HostFunctionError::LEDGER_OBJ_NOT_FOUND);

        sle->setFieldVL(sfData, data);
        v.update(sle);

        return data.size();
    }

    Expected<int32_t, HostFunctionError>
    checkSignature(
        Slice const& message,
        Slice const& signature,
        Slice const& pubkey) override
    {
        if (!publicKeyType(pubkey))
            return Unexpected(HostFunctionError::INVALID_PARAMS);

        PublicKey const pk(pubkey);
        return verify(pk, message, signature);
    }

    Expected<Hash, HostFunctionError>
    computeSha512HalfHash(Slice const& data) override
    {
        auto const hash = sha512Half(data);
        return hash;
    }

    Expected<Bytes, HostFunctionError>
    accountKeylet(AccountID const& account) override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::account(account);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    ammKeylet(Asset const& issue1, Asset const& issue2) override
    {
        if (issue1 == issue2)
            return Unexpected(HostFunctionError::INVALID_PARAMS);

        // note: this should be removed with the MPT DEX amendment
        if (issue1.holds<MPTIssue>() || issue2.holds<MPTIssue>())
            return Unexpected(HostFunctionError::INVALID_PARAMS);

        auto const keylet = keylet::amm(issue1, issue2);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    checkKeylet(AccountID const& account, std::uint32_t seq) override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::check(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    credentialKeylet(
        AccountID const& subject,
        AccountID const& issuer,
        Slice const& credentialType) override
    {
        if (!subject || !issuer)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);

        if (credentialType.empty() ||
            credentialType.size() > maxCredentialTypeLength)
            return Unexpected(HostFunctionError::INVALID_PARAMS);

        auto const keylet = keylet::credential(subject, issuer, credentialType);

        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    didKeylet(AccountID const& account) override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::did(account);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    delegateKeylet(AccountID const& account, AccountID const& authorize)
        override
    {
        if (!account || !authorize)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        if (account == authorize)
            return Unexpected(HostFunctionError::INVALID_PARAMS);
        auto const keylet = keylet::delegate(account, authorize);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    depositPreauthKeylet(AccountID const& account, AccountID const& authorize)
        override
    {
        if (!account || !authorize)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        if (account == authorize)
            return Unexpected(HostFunctionError::INVALID_PARAMS);
        auto const keylet = keylet::depositPreauth(account, authorize);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    escrowKeylet(AccountID const& account, std::uint32_t seq) override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::escrow(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    lineKeylet(
        AccountID const& account1,
        AccountID const& account2,
        Currency const& currency) override
    {
        if (!account1 || !account2)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        if (account1 == account2)
            return Unexpected(HostFunctionError::INVALID_PARAMS);
        if (currency.isZero())
            return Unexpected(HostFunctionError::INVALID_PARAMS);

        auto const keylet = keylet::line(account1, account2, currency);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    mptIssuanceKeylet(AccountID const& issuer, std::uint32_t seq) override
    {
        if (!issuer)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);

        auto const keylet = keylet::mptIssuance(seq, issuer);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    mptokenKeylet(MPTID const& mptid, AccountID const& holder) override
    {
        if (!mptid)
            return Unexpected(HostFunctionError::INVALID_PARAMS);
        if (!holder)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);

        auto const keylet = keylet::mptoken(mptid, holder);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    nftOfferKeylet(AccountID const& account, std::uint32_t seq) override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::nftoffer(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    offerKeylet(AccountID const& account, std::uint32_t seq) override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::offer(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    oracleKeylet(AccountID const& account, std::uint32_t documentId) override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::oracle(account, documentId);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    paychanKeylet(
        AccountID const& account,
        AccountID const& destination,
        std::uint32_t seq) override
    {
        if (!account || !destination)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        if (account == destination)
            return Unexpected(HostFunctionError::INVALID_PARAMS);
        auto const keylet = keylet::payChan(account, destination, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    permissionedDomainKeylet(AccountID const& account, std::uint32_t seq)
        override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::permissionedDomain(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    signersKeylet(AccountID const& account) override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::signers(account);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    ticketKeylet(AccountID const& account, std::uint32_t seq) override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::ticket(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    vaultKeylet(AccountID const& account, std::uint32_t seq) override
    {
        if (!account)
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        auto const keylet = keylet::vault(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    getNFT(AccountID const& account, uint256 const& nftId) override
    {
        if (!account || !nftId)
        {
            getJournal().trace() << "WASM getNFT: Invalid account or NFT ID";
            return Unexpected(HostFunctionError::INVALID_PARAMS);
        }

        auto obj = nft::findToken(*env_.current(), account, nftId);
        if (!obj)
        {
            getJournal().trace() << "WASM getNFT: NFT not found";
            return Unexpected(HostFunctionError::LEDGER_OBJ_NOT_FOUND);
        }

        auto ouri = obj->at(~sfURI);
        if (!ouri)
            return Bytes();

        Slice const s = ouri->value();
        return Bytes(s.begin(), s.end());
    }

    Expected<Bytes, HostFunctionError>
    getNFTIssuer(uint256 const& nftId) override
    {
        auto const issuer = nft::getIssuer(nftId);
        if (!issuer)
            return Unexpected(HostFunctionError::INVALID_PARAMS);

        return Bytes{issuer.begin(), issuer.end()};
    }

    Expected<std::uint32_t, HostFunctionError>
    getNFTTaxon(uint256 const& nftId) override
    {
        return nft::toUInt32(nft::getTaxon(nftId));
    }

    Expected<int32_t, HostFunctionError>
    getNFTFlags(uint256 const& nftId) override
    {
        return nft::getFlags(nftId);
    }

    Expected<int32_t, HostFunctionError>
    getNFTTransferFee(uint256 const& nftId) override
    {
        return nft::getTransferFee(nftId);
    }

    Expected<std::uint32_t, HostFunctionError>
    getNFTSerial(uint256 const& nftId) override
    {
        return nft::getSerial(nftId);
    }
};

}  // namespace test
}  // namespace ripple
