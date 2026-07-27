#include <test/app/wasm_fixtures/fixtures.h>
#include <test/jtx/Env.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/beast/insight/Event.h>
#include <xrpl/beast/insight/EventImpl.h>
#include <xrpl/ledger/AmendmentTable.h>
#include <xrpl/ledger/detail/ApplyViewBase.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <boost/algorithm/hex.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace xrpl::test {

/**
 * Lets a test assert that the WASM execution-timing path fired and inspect the
 * recorded durations, without needing a StatsD sink or a full Collector.
 */
struct RecordingEventImpl : public beast::insight::EventImpl
{
    std::size_t count{};
    value_type last{};
    value_type total{};
    value_type min{value_type::max()};
    value_type max{value_type::min()};
    std::vector<value_type> samples;
    bool printOnDestruction{};

    ~RecordingEventImpl() override
    {
        if (printOnDestruction && count > 0)
        {
            std::cout << "Mean (us): " << meanUs() << "\n";
        }
    }

    void
    notify(value_type const& value) override
    {
        ++count;
        last = value;
        total += value;
        min = std::min(min, value);
        max = std::max(max, value);
        samples.push_back(value);
    }

    [[nodiscard]] double
    meanUs() const
    {
        return count != 0 ? static_cast<double>(total.count()) / static_cast<double>(count) : 0.0;
    }

    [[nodiscard]] value_type
    percentile(double p) const
    {
        if (samples.empty())
        {
            return value_type{};
        }
        auto sorted = samples;
        std::ranges::sort(sorted);
        auto rank = static_cast<std::size_t>((p / 100.0) * static_cast<double>(sorted.size()));
        if (rank >= sorted.size())
        {
            rank = sorted.size() - 1;
        }
        return sorted[rank];
    }
};

struct TestLedgerDataProvider : public HostFunctions
{
    jtx::Env& env;
    void* rt = nullptr;

public:
    TestLedgerDataProvider(jtx::Env& env) : HostFunctions(env.journal), env(env)
    {
    }

    void
    setRT(void* rt) override
    {
        this->rt = rt;
    }

    [[nodiscard]] void*
    getRT() const override
    {
        return rt;
    }

    Expected<std::uint32_t, HostFunctionError>
    getLedgerSqn() const override
    {
        return env.current()->seq();
    }
};

struct TestHostFunctions : public HostFunctions
{
    test::jtx::Env& env;
    AccountID accountID;
    Bytes data;
    int clock_drift = 0;
    void* rt = nullptr;
    std::shared_ptr<RecordingEventImpl> execTimeEvent = std::make_shared<RecordingEventImpl>();

public:
    TestHostFunctions(test::jtx::Env& env, int cd = 0)
        : HostFunctions(env.journal), env(env), clock_drift(cd)
    {
        accountID = env.master.id();
        std::string t = "10000";
        data = Bytes{t.begin(), t.end()};
    }

    void
    setRT(void* rt) override
    {
        this->rt = rt;
    }

    [[nodiscard]] void*
    getRT() const override
    {
        return rt;
    }

    // Return an Event backed by our recording impl so a test can assert that
    // the WASM execution was timed. The name is ignored -- every call records
    // into the same impl.
    [[nodiscard]] beast::insight::Event
    executionTimeEvent(std::string_view name) const override
    {
        return beast::insight::Event(execTimeEvent);
    }

    Expected<std::uint32_t, HostFunctionError>
    getLedgerSqn() const override
    {
        return 12345;
    }

    Expected<std::uint32_t, HostFunctionError>
    getParentLedgerTime() const override
    {
        return 67890;
    }

    Expected<Hash, HostFunctionError>
    getParentLedgerHash() const override
    {
        return env.current()->header().parentHash;
    }

    Expected<std::uint32_t, HostFunctionError>
    getBaseFee() const override
    {
        return 10;
    }

    Expected<int32_t, HostFunctionError>
    isAmendmentEnabled(uint256 const& amendmentId) const override
    {
        return 1;
    }

    Expected<int32_t, HostFunctionError>
    isAmendmentEnabled(std::string_view const& amendmentName) const override
    {
        return 1;
    }

    Expected<int32_t, HostFunctionError>
    cacheLedgerObj(uint256 const& objId, int32_t cacheIdx) override
    {
        return 1;
    }

    Expected<Bytes, HostFunctionError>
    getTxField(SField const& fname) const override
    {
        if (fname == sfAccount)
        {
            return Bytes(accountID.begin(), accountID.end());
        }
        if (fname == sfFee)
        {
            int64_t x = 235;
            uint8_t const* p = reinterpret_cast<uint8_t const*>(&x);
            return Bytes{p, p + sizeof(x)};
        }
        if (fname == sfSequence)
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
    getCurrentLedgerObjField(SField const& fname) const override
    {
        auto const& sn = fname.getName();
        if (sn == "Destination" || sn == "Account")
        {
            return Bytes(accountID.begin(), accountID.end());
        }
        if (sn == "Data")
        {
            return data;
        }
        if (sn == "FinishAfter")
        {
            auto t = env.current()->parentCloseTime().time_since_epoch().count();
            std::string s = std::to_string(t);
            return Bytes{s.begin(), s.end()};
        }

        return Unexpected(HostFunctionError::INTERNAL);
    }

    Expected<Bytes, HostFunctionError>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) const override
    {
        if (fname == sfBalance)
        {
            int64_t x = 10'000;
            uint8_t const* p = reinterpret_cast<uint8_t const*>(&x);
            return Bytes{p, p + sizeof(x)};
        }
        if (fname == sfAccount)
        {
            return Bytes(accountID.begin(), accountID.end());
        }
        return data;
    }

    Expected<Bytes, HostFunctionError>
    getTxNestedField(Slice const& locator) const override
    {
        if (locator.size() == 4)
        {
            int32_t const* l = reinterpret_cast<int32_t const*>(locator.data());
            int32_t const sfield = l[0];
            if (sfield == sfAccount.getCode())
            {
                return Bytes(accountID.begin(), accountID.end());
            }
        }
        uint8_t const a[] = {0x2b, 0x6a, 0x23, 0x2a, 0xa4, 0xc4, 0xbe, 0x41, 0xbf, 0x49, 0xd2,
                             0x45, 0x9f, 0xa4, 0xa0, 0x34, 0x7e, 0x1b, 0x54, 0x3a, 0x4c, 0x92,
                             0xfc, 0xee, 0x08, 0x21, 0xc0, 0x20, 0x1e, 0x2e, 0x9a, 0x00};
        return Bytes(&a[0], &a[sizeof(a)]);
    }

    Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjNestedField(Slice const& locator) const override
    {
        if (locator.size() == 4)
        {
            int32_t const* l = reinterpret_cast<int32_t const*>(locator.data());
            int32_t const sfield = l[0];
            if (sfield == sfAccount.getCode())
            {
                return Bytes(accountID.begin(), accountID.end());
            }
        }
        uint8_t const a[] = {0x2b, 0x6a, 0x23, 0x2a, 0xa4, 0xc4, 0xbe, 0x41, 0xbf, 0x49, 0xd2,
                             0x45, 0x9f, 0xa4, 0xa0, 0x34, 0x7e, 0x1b, 0x54, 0x3a, 0x4c, 0x92,
                             0xfc, 0xee, 0x08, 0x21, 0xc0, 0x20, 0x1e, 0x2e, 0x9a, 0x00};
        return Bytes(&a[0], &a[sizeof(a)]);
    }

    Expected<Bytes, HostFunctionError>
    getLedgerObjNestedField(int32_t cacheIdx, Slice const& locator) const override
    {
        if (locator.size() == 4)
        {
            int32_t const* l = reinterpret_cast<int32_t const*>(locator.data());
            int32_t const sfield = l[0];
            if (sfield == sfAccount.getCode())
            {
                return Bytes(accountID.begin(), accountID.end());
            }
        }
        uint8_t const a[] = {0x2b, 0x6a, 0x23, 0x2a, 0xa4, 0xc4, 0xbe, 0x41, 0xbf, 0x49, 0xd2,
                             0x45, 0x9f, 0xa4, 0xa0, 0x34, 0x7e, 0x1b, 0x54, 0x3a, 0x4c, 0x92,
                             0xfc, 0xee, 0x08, 0x21, 0xc0, 0x20, 0x1e, 0x2e, 0x9a, 0x00};
        return Bytes(&a[0], &a[sizeof(a)]);
    }

    Expected<int32_t, HostFunctionError>
    getTxArrayLen(SField const& fname) const override
    {
        return 32;
    }

    Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjArrayLen(SField const& fname) const override
    {
        return 32;
    }

    Expected<int32_t, HostFunctionError>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname) const override
    {
        return 32;
    }

    Expected<int32_t, HostFunctionError>
    getTxNestedArrayLen(Slice const& locator) const override
    {
        return 32;
    }

    Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjNestedArrayLen(Slice const& locator) const override
    {
        return 32;
    }

    Expected<int32_t, HostFunctionError>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, Slice const& locator) const override
    {
        return 32;
    }

    Expected<int32_t, HostFunctionError>
    updateData(Slice const& data) override
    {
        return data.size();
    }

    Expected<int32_t, HostFunctionError>
    checkSignature(Slice const& message, Slice const& signature, Slice const& pubkey) const override
    {
        return 1;
    }

    Expected<Hash, HostFunctionError>
    computeSha512HalfHash(Slice const& data) const override
    {
        return env.current()->header().parentHash;
    }

    Expected<Bytes, HostFunctionError>
    accountKeylet(AccountID const& account) const override
    {
        if (!account)
            return Unexpected(HostFunctionError::InvalidAccount);
        auto const keylet = keylet::account(account);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    ammKeylet(Asset const& issue1, Asset const& issue2) const override
    {
        if (issue1 == issue2)
            return Unexpected(HostFunctionError::InvalidParams);
        if (issue1.holds<MPTIssue>() || issue2.holds<MPTIssue>())
            return Unexpected(HostFunctionError::InvalidParams);
        auto const keylet = keylet::amm(issue1, issue2);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    checkKeylet(AccountID const& account, std::uint32_t seq) const override
    {
        if (!account)
            return Unexpected(HostFunctionError::InvalidAccount);
        auto const keylet = keylet::check(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    credentialKeylet(AccountID const& subject, AccountID const& issuer, Slice const& credentialType)
        const override
    {
        if (!subject || !issuer || credentialType.empty() ||
            credentialType.size() > kMaxCredentialTypeLength)
            return Unexpected(HostFunctionError::InvalidAccount);
        auto const keylet = keylet::credential(subject, issuer, credentialType);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    escrowKeylet(AccountID const& account, std::uint32_t seq) const override
    {
        if (!account)
            return Unexpected(HostFunctionError::InvalidAccount);
        auto const keylet = keylet::escrow(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    oracleKeylet(AccountID const& account, std::uint32_t documentId) const override
    {
        if (!account)
            return Unexpected(HostFunctionError::InvalidAccount);
        auto const keylet = keylet::oracle(account, documentId);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    Expected<Bytes, HostFunctionError>
    getNFT(AccountID const& account, uint256 const& nftId) const override
    {
        if (!account || !nftId)
        {
            return Unexpected(HostFunctionError::InvalidParams);
        }

        std::string s = "https://ripple.com";
        return Bytes(s.begin(), s.end());
    }

    Expected<Bytes, HostFunctionError>
    getNFTIssuer(uint256 const& nftId) const override
    {
        return Bytes(accountID.begin(), accountID.end());
    }

    Expected<std::uint32_t, HostFunctionError>
    getNFTTaxon(uint256 const& nftId) const override
    {
        return 4;
    }

    Expected<int32_t, HostFunctionError>
    getNFTFlags(uint256 const& nftId) const override
    {
        return 8;
    }

    Expected<int32_t, HostFunctionError>
    getNFTTransferFee(uint256 const& nftId) const override
    {
        return 10;
    }

    Expected<std::uint32_t, HostFunctionError>
    getNFTSerial(uint256 const& nftId) const override
    {
        return 4;
    }

    template <typename F>
    void
    log(std::string_view const& msg, F&& dataFn) const
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        if (!getJournal().active(beast::Severity::Trace))
            return;
        auto j = getJournal().trace();
#endif
        j << "WasmTrace: " << msg << " " << dataFn();

#ifdef DEBUG_OUTPUT
        j << std::endl;
#endif
    }

    Expected<int32_t, HostFunctionError>
    trace(std::string_view const& msg, Slice const& data, bool asHex) const override
    {
        if (!asHex)
        {
            log(msg, [&data] {
                return std::string_view(reinterpret_cast<char const*>(data.data()), data.size());
            });
        }
        else
        {
            log(msg, [&data] {
                std::string hex;
                hex.reserve(data.size() * 2);
                boost::algorithm::hex(data.begin(), data.end(), std::back_inserter(hex));
                return hex;
            });
        }

        return 0;
    }

    Expected<int32_t, HostFunctionError>
    traceNum(std::string_view const& msg, int64_t data) const override
    {
        log(msg, [data] { return data; });
        return 0;
    }

    Expected<int32_t, HostFunctionError>
    traceAccount(std::string_view const& msg, AccountID const& account) const override
    {
        log(msg, [&account] { return toBase58(account); });
        return 0;
    }

    Expected<int32_t, HostFunctionError>
    traceFloat(std::string_view const& msg, Slice const& data) const override
    {
        log(msg, [&data] { return wasm_float::floatToString(data); });
        return 0;
    }

    Expected<int32_t, HostFunctionError>
    traceAmount(std::string_view const& msg, STAmount const& amount) const override
    {
        log(msg, [&amount] { return amount.getFullText(); });
        return 0;
    }

    Expected<Bytes, HostFunctionError>
    floatFromInt(int64_t x, int32_t mode) const override
    {
        return wasm_float::floatFromIntImpl(x, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatFromUint(uint64_t x, int32_t mode) const override
    {
        return wasm_float::floatFromUintImpl(x, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatFromSTAmount(STAmount const& x, int32_t mode) const override
    {
        return wasm_float::floatFromSTAmountImpl(x, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatFromSTNumber(STNumber const& x, int32_t mode) const override
    {
        return wasm_float::floatFromSTNumberImpl(x, mode);
    }

    Expected<int64_t, HostFunctionError>
    floatToInt(Slice const& x, int32_t mode) const override
    {
        return wasm_float::floatToIntImpl(x, mode);
    }

    Expected<FloatPair, HostFunctionError>
    floatToMantExp(Slice const& x) const override
    {
        return wasm_float::floatToMantExpImpl(x);
    }

    Expected<Bytes, HostFunctionError>
    floatFromMantExp(int64_t mantissa, int32_t exponent, int32_t mode) const override
    {
        return wasm_float::floatFromMantExpImpl(mantissa, exponent, mode);
    }

    Expected<int32_t, HostFunctionError>
    floatCompare(Slice const& x, Slice const& y) const override
    {
        return wasm_float::floatCompareImpl(x, y);
    }

    Expected<Bytes, HostFunctionError>
    floatAdd(Slice const& x, Slice const& y, int32_t mode) const override
    {
        return wasm_float::floatAddImpl(x, y, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatSubtract(Slice const& x, Slice const& y, int32_t mode) const override
    {
        return wasm_float::floatSubtractImpl(x, y, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatMultiply(Slice const& x, Slice const& y, int32_t mode) const override
    {
        return wasm_float::floatMultiplyImpl(x, y, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatDivide(Slice const& x, Slice const& y, int32_t mode) const override
    {
        return wasm_float::floatDivideImpl(x, y, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatRoot(Slice const& x, int32_t n, int32_t mode) const override
    {
        return wasm_float::floatRootImpl(x, n, mode);
    }

    Expected<Bytes, HostFunctionError>
    floatPower(Slice const& x, int32_t n, int32_t mode) const override
    {
        return wasm_float::floatPowerImpl(x, n, mode);
    }
};

struct TestHostFunctionsSink : public TestHostFunctions
{
    test::StreamSink sink;
    void const* rt = nullptr;

public:
    explicit TestHostFunctionsSink(test::jtx::Env& env, int cd = 0)
        : TestHostFunctions(env, cd), sink(beast::Severity::Debug)
    {
        j = beast::Journal(sink);
    }

    test::StreamSink&
    getSink()
    {
        return sink;
    }
};

}  // namespace xrpl::test
