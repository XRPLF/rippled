#pragma once

#include <test/jtx/Env.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/detail/ApplyViewBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <boost/algorithm/hex.hpp>

#include <cstdint>
#include <expected>
#include <iterator>
#include <string>
#include <string_view>

namespace xrpl::test {

class TestLedgerDataProvider : public HostFunctions
{
    jtx::Env& env_;

public:
    TestLedgerDataProvider(jtx::Env& env) : HostFunctions(env.journal), env_(env)
    {
    }

    [[nodiscard]] std::expected<std::uint32_t, HostFunctionError>
    getLedgerSqn() const override
    {
        return env_.current()->seq();
    }
};

class TestHostFunctions : public HostFunctions
{
protected:
    test::jtx::Env& env_;
    AccountID accountID_;
    Bytes data_;

public:
    TestHostFunctions(test::jtx::Env& env) : HostFunctions(env.journal), env_(env)
    {
        accountID_ = env.master.id();
        std::string t = "10000";
        data_ = Bytes{t.begin(), t.end()};
    }

    [[nodiscard]] std::expected<std::uint32_t, HostFunctionError>
    getLedgerSqn() const override
    {
        return 12345;
    }

    [[nodiscard]] std::expected<std::uint32_t, HostFunctionError>
    getParentLedgerTime() const override
    {
        return 67890;
    }

    [[nodiscard]] std::expected<Hash, HostFunctionError>
    getParentLedgerHash() const override
    {
        return env_.current()->header().parentHash;
    }

    [[nodiscard]] std::expected<std::uint32_t, HostFunctionError>
    getBaseFee() const override
    {
        return 10;
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    isAmendmentEnabled(uint256 const& amendmentId) const override
    {
        return 1;
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    isAmendmentEnabled(std::string_view const& amendmentName) const override
    {
        return 1;
    }

    std::expected<int32_t, HostFunctionError>
    cacheLedgerObj(uint256 const& objId, int32_t cacheIdx) override
    {
        return 1;
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    getTxField(SField const& fname) const override
    {
        if (fname == sfAccount)
            return Bytes(accountID_.begin(), accountID_.end());

        if (fname == sfFee)
        {
            int64_t x = 235;
            auto const* p = reinterpret_cast<uint8_t const*>(&x);
            return Bytes{p, p + sizeof(x)};
        }

        if (fname == sfSequence)
        {
            auto const x = getLedgerSqn();
            if (!x)
                return std::unexpected(x.error());
            std::uint32_t const data = x.value();
            auto const* b = reinterpret_cast<uint8_t const*>(&data);
            auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
            return Bytes{b, e};
        }

        return Bytes();
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    getCurrentLedgerObjField(SField const& fname) const override
    {
        auto const& sn = fname.getName();
        if (sn == "Destination" || sn == "Account")
            return Bytes(accountID_.begin(), accountID_.end());
        if (sn == "Data")
            return data_;
        if (sn == "FinishAfter")
        {
            auto t = env_.current()->parentCloseTime().time_since_epoch().count();
            std::string s = std::to_string(t);
            return Bytes{s.begin(), s.end()};
        }

        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    getLedgerObjField(int32_t, SField const& fname) const override
    {
        if (fname == sfBalance)
        {
            int64_t x = 10'000;
            auto const* p = reinterpret_cast<uint8_t const*>(&x);
            return Bytes{p, p + sizeof(x)};
        }

        if (fname == sfAccount)
            return Bytes(accountID_.begin(), accountID_.end());

        return data_;
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    getTxNestedField(FieldLocator const& locator) const override
    {
        if (locator.size() == 1)
        {
            int32_t const* l = locator.data();
            int32_t const sfield = l[0];
            if (sfield == sfAccount.getCode())
                return Bytes(accountID_.begin(), accountID_.end());
        }

        uint8_t const a[] = {0x2b, 0x6a, 0x23, 0x2a, 0xa4, 0xc4, 0xbe, 0x41, 0xbf, 0x49, 0xd2,
                             0x45, 0x9f, 0xa4, 0xa0, 0x34, 0x7e, 0x1b, 0x54, 0x3a, 0x4c, 0x92,
                             0xfc, 0xee, 0x08, 0x21, 0xc0, 0x20, 0x1e, 0x2e, 0x9a, 0x00};
        return Bytes(&a[0], &a[sizeof(a)]);
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    getCurrentLedgerObjNestedField(FieldLocator const& locator) const override
    {
        if (locator.size() == 1)
        {
            int32_t const* l = locator.data();
            int32_t const sfield = l[0];
            if (sfield == sfAccount.getCode())
                return Bytes(accountID_.begin(), accountID_.end());
        }

        uint8_t const a[] = {0x2b, 0x6a, 0x23, 0x2a, 0xa4, 0xc4, 0xbe, 0x41, 0xbf, 0x49, 0xd2,
                             0x45, 0x9f, 0xa4, 0xa0, 0x34, 0x7e, 0x1b, 0x54, 0x3a, 0x4c, 0x92,
                             0xfc, 0xee, 0x08, 0x21, 0xc0, 0x20, 0x1e, 0x2e, 0x9a, 0x00};
        return Bytes(&a[0], &a[sizeof(a)]);
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    getLedgerObjNestedField(int32_t cacheIdx, FieldLocator const& locator) const override
    {
        if (locator.size() == 1)
        {
            int32_t const* l = locator.data();
            int32_t const sfield = l[0];
            if (sfield == sfAccount.getCode())
                return Bytes(accountID_.begin(), accountID_.end());
        }

        uint8_t const a[] = {0x2b, 0x6a, 0x23, 0x2a, 0xa4, 0xc4, 0xbe, 0x41, 0xbf, 0x49, 0xd2,
                             0x45, 0x9f, 0xa4, 0xa0, 0x34, 0x7e, 0x1b, 0x54, 0x3a, 0x4c, 0x92,
                             0xfc, 0xee, 0x08, 0x21, 0xc0, 0x20, 0x1e, 0x2e, 0x9a, 0x00};
        return Bytes(&a[0], &a[sizeof(a)]);
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    getTxArrayLen(SField const& fname) const override
    {
        return 32;
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    getCurrentLedgerObjArrayLen(SField const& fname) const override
    {
        return 32;
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname) const override
    {
        return 32;
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    getTxNestedArrayLen(FieldLocator const& locator) const override
    {
        return 32;
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    getCurrentLedgerObjNestedArrayLen(FieldLocator const& locator) const override
    {
        return 32;
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, FieldLocator const& locator) const override
    {
        return 32;
    }

    std::expected<int32_t, HostFunctionError>
    updateData(Slice const& data) override
    {
        return data.size();
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    checkSignature(Slice const& message, Slice const& signature, Slice const& pubkey) const override
    {
        return 1;
    }

    [[nodiscard]] std::expected<Hash, HostFunctionError>
    computeSha512HalfHash(Slice const& data) const override
    {
        return env_.current()->header().parentHash;
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    accountKeylet(AccountID const& account) const override
    {
        if (!account)
            return std::unexpected(HostFunctionError::InvalidAccount);
        auto const keylet = keylet::account(account);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    ammKeylet(Asset const& issue1, Asset const& issue2) const override
    {
        if (issue1 == issue2)
            return std::unexpected(HostFunctionError::InvalidParams);
        if (issue1.holds<MPTIssue>() || issue2.holds<MPTIssue>())
            return std::unexpected(HostFunctionError::InvalidParams);
        auto const keylet = keylet::amm(issue1, issue2);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    checkKeylet(AccountID const& account, std::uint32_t seq) const override
    {
        if (!account)
            return std::unexpected(HostFunctionError::InvalidAccount);
        auto const keylet = keylet::check(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    credentialKeylet(AccountID const& subject, AccountID const& issuer, Slice const& credentialType)
        const override
    {
        if (!subject || !issuer || credentialType.empty() ||
            credentialType.size() > kMaxCredentialTypeLength)
            return std::unexpected(HostFunctionError::InvalidAccount);
        auto const keylet = keylet::credential(subject, issuer, credentialType);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    escrowKeylet(AccountID const& account, std::uint32_t seq) const override
    {
        if (!account)
            return std::unexpected(HostFunctionError::InvalidAccount);
        auto const keylet = keylet::escrow(account, seq);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    oracleKeylet(AccountID const& account, std::uint32_t documentId) const override
    {
        if (!account)
            return std::unexpected(HostFunctionError::InvalidAccount);
        auto const keylet = keylet::oracle(account, documentId);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    getNFT(AccountID const& account, uint256 const& nftId) const override
    {
        if (!account || !nftId)
            return std::unexpected(HostFunctionError::InvalidParams);

        std::string s = "https://ripple.com";
        return Bytes(s.begin(), s.end());
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    getNFTIssuer(uint256 const& nftId) const override
    {
        return Bytes(accountID_.begin(), accountID_.end());
    }

    [[nodiscard]] std::expected<std::uint32_t, HostFunctionError>
    getNFTTaxon(uint256 const& nftId) const override
    {
        return 4;
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    getNFTFlags(uint256 const& nftId) const override
    {
        return 8;
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    getNFTTransferFee(uint256 const& nftId) const override
    {
        return 10;
    }

    [[nodiscard]] std::expected<std::uint32_t, HostFunctionError>
    getNFTSequence(uint256 const& nftId) const override
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

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
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

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    traceNum(std::string_view const& msg, int64_t data) const override
    {
        log(msg, [data] { return data; });
        return 0;
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    traceAccount(std::string_view const& msg, AccountID const& account) const override
    {
        log(msg, [&account] { return toBase58(account); });
        return 0;
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    traceFloat(std::string_view const& msg, Slice const& data) const override
    {
        log(msg, [&data] { return wasm_float::floatToString(data); });
        return 0;
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    traceAmount(std::string_view const& msg, STAmount const& amount) const override
    {
        log(msg, [&amount] { return amount.getFullText(); });
        return 0;
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    floatFromInt(int64_t x, int32_t mode) const override
    {
        return wasm_float::floatFromIntImpl(x, mode);
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    floatFromUint(uint64_t x, int32_t mode) const override
    {
        return wasm_float::floatFromUintImpl(x, mode);
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    floatFromSTAmount(STAmount const& x, int32_t mode) const override
    {
        return wasm_float::floatFromSTAmountImpl(x, mode);
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    floatFromSTNumber(STNumber const& x, int32_t mode) const override
    {
        return wasm_float::floatFromSTNumberImpl(x, mode);
    }

    [[nodiscard]] std::expected<int64_t, HostFunctionError>
    floatToInt(Slice const& x, int32_t mode) const override
    {
        return wasm_float::floatToIntImpl(x, mode);
    }

    [[nodiscard]] std::expected<FloatPair, HostFunctionError>
    floatToMantExp(Slice const& x) const override
    {
        return wasm_float::floatToMantExpImpl(x);
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    floatFromMantExp(int64_t mantissa, int32_t exponent, int32_t mode) const override
    {
        return wasm_float::floatFromMantExpImpl(mantissa, exponent, mode);
    }

    [[nodiscard]] std::expected<int32_t, HostFunctionError>
    floatCompare(Slice const& x, Slice const& y) const override
    {
        return wasm_float::floatCompareImpl(x, y);
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    floatAdd(Slice const& x, Slice const& y, int32_t mode) const override
    {
        return wasm_float::floatAddImpl(x, y, mode);
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    floatSubtract(Slice const& x, Slice const& y, int32_t mode) const override
    {
        return wasm_float::floatSubtractImpl(x, y, mode);
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    floatMultiply(Slice const& x, Slice const& y, int32_t mode) const override
    {
        return wasm_float::floatMultiplyImpl(x, y, mode);
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    floatDivide(Slice const& x, Slice const& y, int32_t mode) const override
    {
        return wasm_float::floatDivideImpl(x, y, mode);
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    floatRoot(Slice const& x, int32_t n, int32_t mode) const override
    {
        return wasm_float::floatRootImpl(x, n, mode);
    }

    [[nodiscard]] std::expected<Bytes, HostFunctionError>
    floatPower(Slice const& x, int32_t n, int32_t mode) const override
    {
        return wasm_float::floatPowerImpl(x, n, mode);
    }
};

class TestHostFunctionsSink : public TestHostFunctions
{
    test::StreamSink sink_;

public:
    explicit TestHostFunctionsSink(test::jtx::Env& env)
        : TestHostFunctions(env), sink_(beast::Severity::Debug)
    {
        j_ = beast::Journal(sink_);
    }

    test::StreamSink&
    getSink()
    {
        return sink_;
    }
};

}  // namespace xrpl::test
