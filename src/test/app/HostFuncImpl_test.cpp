#include <test/jtx.h>

#include <xrpl/protocol/digest.h>
#include <xrpl/tx/wasm/HostFuncImpl.h>
#include <xrpl/tx/wasm/HostFuncWrapper.h>
#include <xrpl/tx/wasm/WasmiVM.h>

namespace xrpl {
namespace test {

static Bytes
toBytes(std::uint8_t value)
{
    return {value};
}

static Bytes
toBytes(std::uint16_t value)
{
    auto const* b = reinterpret_cast<uint8_t const*>(&value);
    auto const* e = reinterpret_cast<uint8_t const*>(&value + 1);
    return Bytes{b, e};
}

static Bytes
toBytes(std::uint32_t value)
{
    auto const* b = reinterpret_cast<uint8_t const*>(&value);
    auto const* e = reinterpret_cast<uint8_t const*>(&value + 1);
    return Bytes{b, e};
}

static Bytes
toBytes(uint256 const& value)
{
    return Bytes{value.begin(), value.end()};
}

static Bytes
toBytes(Issue const& issue)
{
    Serializer s;
    s.addBitString(issue.currency);
    if (!isXRP(issue.currency))
        s.addBitString(issue.account);
    auto const data = s.getData();
    return data;
}

static Bytes
toBytes(Asset const& asset)
{
    if (asset.holds<Issue>())
        return toBytes(asset.get<Issue>());

    auto const& mptIssue = asset.get<MPTIssue>();
    auto const& mptID = mptIssue.getMptID();
    return Bytes{mptID.cbegin(), mptID.cend()};
}

static Bytes
toBytes(STAmount const& amount)
{
    Serializer msg;
    amount.add(msg);
    auto const data = msg.getData();

    return data;
}

static Bytes
toBytes(STNumber const& number)
{
    Serializer msg;
    number.add(msg);
    auto const data = msg.getData();

    return data;
}

static ApplyContext
createApplyContext(
    test::jtx::Env& env,
    OpenView& ov,
    beast::Journal j,
    STTx const& tx = STTx(ttESCROW_FINISH, [](STObject&) {}))
{
    ApplyContext ac{env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, tapNONE, j};
    return ac;
}

static ApplyContext
createApplyContext(
    test::jtx::Env& env,
    OpenView& ov,
    STTx const& tx = STTx(ttESCROW_FINISH, [](STObject&) {}))
{
    return createApplyContext(env, ov, env.journal, tx);
}

class VirtualRuntime : public WasmRuntimeWrapper
{
    Bytes buffer_;
    std::int64_t gas_ = 1'000'000;

public:
    VirtualRuntime() : buffer_(1024 * 1024)
    {
    }

    wmem
    getMem() override
    {
        return {buffer_.data(), buffer_.size()};
    }

    std::int64_t
    getGas() override
    {
        gas_ -= 100;
        return gas_;
    }

    std::int64_t
    setGas(std::int64_t gas) override
    {
        if (gas == -2)
            return -1;

        if (gas < 0)
        {
            gas_ = std::numeric_limits<decltype(gas)>::max();
        }
        else
        {
            gas_ = gas;
        }

        return gas_;
    }

    void
    checkIdx(WasmValVec const& params, size_t i) const
    {
        if (i + 1 >= params.size())
            Throw<std::runtime_error>("Out of bounds");
        if (params[i].kind != WASM_I32 || params[i + 1].kind != WASM_I32)
            Throw<std::runtime_error>("Invalid params");
        std::int32_t const ptr = params[i].of.i32;
        std::int32_t const size = params[i + 1].of.i32;
        std::int64_t const offset = (std::int64_t)ptr + size;
        if (ptr < 0 || size < 0 || std::cmp_greater_equal(offset, buffer_.size()))
            Throw<std::runtime_error>("Out of bounds");
    }

    Slice
    getBuffer(WasmValVec const& params, size_t i) const
    {
        checkIdx(params, i);
        std::int32_t const ptr = params[i].of.i32;
        std::int32_t const size = params[i + 1].of.i32;
        return {&buffer_[ptr], static_cast<size_t>(size)};
    }

    Bytes
    getBytes(WasmValVec const& params, size_t i) const
    {
        checkIdx(params, i);
        std::int32_t const ptr = params[i].of.i32;
        std::int32_t const size = params[i + 1].of.i32;
        return {&buffer_[ptr], &buffer_[ptr + size]};
    }

    void
    setBytes(size_t ptr, void const* bytes, size_t size)
    {
        if (ptr + size >= buffer_.size())
            Throw<std::runtime_error>("Out of bounds");
        memcpy(&buffer_[ptr], bytes, size);
    }

    template <class T>
    T
    getInt(WasmValVec const& params, size_t i) const
    {
        checkIdx(params, i);
        std::int32_t const ptr = params[i].of.i32;
        std::int32_t const size = params[i + 1].of.i32;
        if (size != sizeof(T))
            Throw<std::runtime_error>("Invalid size");
        return *reinterpret_cast<T const*>(&buffer_[ptr]);
    }

    std::int32_t
    getInt32(WasmValVec const& params, size_t i) const
    {
        return getInt<std::int32_t>(params, i);
    }

    std::uint32_t
    getUint32(WasmValVec const& params, size_t i) const
    {
        return getInt<std::uint32_t>(params, i);
    }

    std::int64_t
    getInt64(WasmValVec const& params, size_t i) const
    {
        return getInt<std::int64_t>(params, i);
    }

    std::uint64_t
    getUint64(WasmValVec const& params, size_t i) const
    {
        return getInt<std::uint64_t>(params, i);
    }
};

template <class P, class E, typename Arg>
void
ww_hlp(size_t& idx, E&& e, P&& params, Arg&& arg)
{
    if constexpr (std::is_integral_v<Arg>)
    {
        params[idx++] = std::is_same_v<Arg, int64_t> || std::is_same_v<Arg, long long>
            ? wasm_val_t WASM_I64_VAL(static_cast<int64_t>(arg))
            : wasm_val_t WASM_I32_VAL(static_cast<int32_t>(arg));
    }
    else if constexpr (std::is_same_v<Arg, Issue>)
    {
        auto const* udata = reinterpret_cast<WasmUserData*>(e);
        HostFunctions const* hf = reinterpret_cast<HostFunctions*>(udata->first);
        auto* vrt = reinterpret_cast<VirtualRuntime*>(hf->getRT());

        auto const data = toBytes(std::forward<Arg>(arg));

        size_t const ptr = (idx << 10);
        vrt->setBytes(ptr, data.data(), data.size());
        params[idx++] = wasm_val_t WASM_I32_VAL(static_cast<int32_t>(ptr));
        params[idx++] = wasm_val_t WASM_I32_VAL(static_cast<int32_t>(data.size()));
    }
    else
    {
        auto const* udata = reinterpret_cast<WasmUserData*>(e);
        HostFunctions const* hf = reinterpret_cast<HostFunctions*>(udata->first);
        auto* vrt = reinterpret_cast<VirtualRuntime*>(hf->getRT());

        size_t const ptr = (idx << 10);
        vrt->setBytes(ptr, arg.data(), arg.size());
        params[idx++] = wasm_val_t WASM_I32_VAL(static_cast<int32_t>(ptr));
        params[idx++] = wasm_val_t WASM_I32_VAL(static_cast<int32_t>(arg.size()));
    }
}

// Helper wrapper to call WASM wrapper functions with automatic parameter packing
template <class F, class E, class P, typename... Args>
wasm_trap_t*
ww(F&& f, E&& e, P&& params, P&& result, Args... args)
{
    size_t idx = 0;
    (ww_hlp(idx, std::forward<E>(e), std::forward<P>(params), std::forward<Args>(args)),
     ...);                                                     // NOLINT
    return f(std::forward<E>(e), params.get(), result.get());  // NOLINT
}

constexpr int64_t min64 = std::numeric_limits<int64_t>::min();
constexpr int64_t max64 = std::numeric_limits<int64_t>::max();
constexpr int32_t FLOAT_SIZE = 12;

struct HostFuncImpl_test : public beast::unit_test::suite
{
    void
    testGetLedgerSqn()
    {
        testcase("getLedgerSqn");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.getLedgerSqn();
            WasmValVec params(2), result(1);
            auto* trap =
                ww(getLedgerSqn_wrap, &import[0], params, result, 0, sizeof(std::uint32_t));

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == sizeof(std::uint32_t)) &&
                BEAST_EXPECT(vrt.getUint32(params, 0) == env.current()->header().seq);
        }
    }

    void
    testGetParentLedgerTime()
    {
        testcase("getParentLedgerTime");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.getParentLedgerTime();
            WasmValVec params(2), result(1);
            auto* trap =
                ww(getParentLedgerTime_wrap, &import[1], params, result, 0, sizeof(std::uint32_t));

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == sizeof(std::uint32_t)) &&
                BEAST_EXPECT(
                    vrt.getUint32(params, 0) ==
                    env.current()->parentCloseTime().time_since_epoch().count());
        }
    }

    void
    testGetParentLedgerHash()
    {
        testcase("getParentLedgerHash");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.getParentLedgerHash();
            WasmValVec params(2), result(1);
            auto* trap =
                ww(getParentLedgerHash_wrap, &import[2], params, result, 0, uint256::bytes);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == uint256::bytes);
            auto const resultBytes = vrt.getBytes(params, 0);
            auto const expectedHash = env.current()->header().parentHash;
            BEAST_EXPECT(
                resultBytes.size() == uint256::bytes &&
                std::memcmp(resultBytes.data(), expectedHash.data(), uint256::bytes) == 0);
        }
    }

    void
    testGetBaseFee()
    {
        testcase("getBaseFee");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // hfs.getBaseFee();
        {
            WasmValVec params(2), result(1);
            auto* trap = ww(getBaseFee_wrap, &import[3], params, result, 0, sizeof(std::uint32_t));

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == sizeof(std::uint32_t)) &&
                BEAST_EXPECT(vrt.getUint32(params, 0) == env.current()->fees().base.drops());
        }
    }

    void
    testIsAmendmentEnabled()
    {
        testcase("isAmendmentEnabled");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // Use featureTokenEscrow for testing
        auto const amendmentId = featureTokenEscrow;

        // hfs.isAmendmentEnabled(amendmentId);
        {
            WasmValVec params(2), result(1);
            vrt.setBytes(0, amendmentId.data(), uint256::bytes);
            auto* trap = ww(isAmendmentEnabled_wrap, &import[4], params, result, 0, uint256::bytes);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 1);
        }

        std::string const amendmentName = "TokenEscrow";
        // hfs.isAmendmentEnabled(amendmentName);
        {
            WasmValVec params(2), result(1);
            vrt.setBytes(0, amendmentName.data(), amendmentName.size());
            auto* trap =
                ww(isAmendmentEnabled_wrap, &import[4], params, result, 0, amendmentName.size());

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 1);
        }

        uint256 const fakeId;
        // hfs.isAmendmentEnabled(fakeId);
        {
            WasmValVec params(2), result(1);
            vrt.setBytes(0, fakeId.data(), uint256::bytes);
            auto* trap = ww(isAmendmentEnabled_wrap, &import[4], params, result, 0, uint256::bytes);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 0);
        }

        std::string const fakeName = "FakeAmendment";
        // hfs.isAmendmentEnabled(fakeName);
        {
            WasmValVec params(2), result(1);
            vrt.setBytes(0, fakeName.data(), fakeName.size());
            auto* trap =
                ww(isAmendmentEnabled_wrap, &import[4], params, result, 0, fakeName.size());

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 0);
        }
    }

    void
    testCacheLedgerObj()
    {
        testcase("cacheLedgerObj");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, 2);
        auto const accountKeylet = keylet::account(env.master);
        {
            VirtualRuntime vrt;
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            // hfs.cacheLedgerObj(accountKeylet.key, -1);
            {
                WasmValVec params(3), result(1);
                vrt.setBytes(0, accountKeylet.key.data(), uint256::bytes);
                auto* trap =
                    ww(cacheLedgerObj_wrap, &import[5], params, result, 0, uint256::bytes, -1);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(
                        result[0].of.i32 ==
                        static_cast<int32_t>(HostFunctionError::SLOT_OUT_RANGE));
            }

            // hfs.cacheLedgerObj(accountKeylet.key, 257);
            {
                WasmValVec params(3), result(1);
                vrt.setBytes(0, accountKeylet.key.data(), uint256::bytes);
                auto* trap =
                    ww(cacheLedgerObj_wrap, &import[5], params, result, 0, uint256::bytes, 257);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(
                        result[0].of.i32 ==
                        static_cast<int32_t>(HostFunctionError::SLOT_OUT_RANGE));
            }

            // hfs.cacheLedgerObj(dummyEscrow.key, 0);
            {
                WasmValVec params(3), result(1);
                vrt.setBytes(0, dummyEscrow.key.data(), uint256::bytes);
                auto* trap =
                    ww(cacheLedgerObj_wrap, &import[5], params, result, 0, uint256::bytes, 0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(
                        result[0].of.i32 ==
                        static_cast<int32_t>(HostFunctionError::LEDGER_OBJ_NOT_FOUND));
            }

            // hfs.cacheLedgerObj(accountKeylet.key, 0);
            {
                WasmValVec params(3), result(1);
                vrt.setBytes(0, accountKeylet.key.data(), uint256::bytes);
                auto* trap =
                    ww(cacheLedgerObj_wrap, &import[5], params, result, 0, uint256::bytes, 0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == 1);
            }

            vrt.setGas(2'000'000);
            for (int i = 1; i <= 256; ++i)
            {
                // hfs.cacheLedgerObj(accountKeylet.key, i);
                WasmValVec params(3), result(1);
                vrt.setBytes(0, accountKeylet.key.data(), uint256::bytes);
                auto* trap =
                    ww(cacheLedgerObj_wrap, &import[5], params, result, 0, uint256::bytes, i);

                if (!(BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                      BEAST_EXPECTS(
                          result[0].of.i32 == i,
                          "result: " + std::to_string(result[0].of.i32) +
                              ", expected: " + std::to_string(i))))
                    break;
            }

            // hfs.cacheLedgerObj(accountKeylet.key, 0);
            {
                WasmValVec params(3), result(1);
                vrt.setBytes(0, accountKeylet.key.data(), uint256::bytes);
                auto* trap =
                    ww(cacheLedgerObj_wrap, &import[5], params, result, 0, uint256::bytes, 0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(
                        result[0].of.i32 == static_cast<int32_t>(HostFunctionError::SLOTS_FULL));
            }
        }

        {
            VirtualRuntime vrt;
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            vrt.setGas(2'000'000);
            for (int i = 1; i <= 256; ++i)
            {
                // hfs.cacheLedgerObj(accountKeylet.key, 0);
                WasmValVec params(3), result(1);
                vrt.setBytes(0, accountKeylet.key.data(), uint256::bytes);
                auto* trap =
                    ww(cacheLedgerObj_wrap, &import[5], params, result, 0, uint256::bytes, 0);

                if (!(BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                      BEAST_EXPECTS(
                          result[0].of.i32 == i,
                          "result: " + std::to_string(result[0].of.i32) +
                              ", expected: " + std::to_string(i))))
                    break;
            }

            // hfs.cacheLedgerObj(accountKeylet.key, 0);
            {
                WasmValVec params(3), result(1);
                vrt.setBytes(0, accountKeylet.key.data(), uint256::bytes);
                auto* trap =
                    ww(cacheLedgerObj_wrap, &import[5], params, result, 0, uint256::bytes, 0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(
                        result[0].of.i32 == static_cast<int32_t>(HostFunctionError::SLOTS_FULL));
            }
        }
    }

    void
    testGetTxField()
    {
        testcase("getTxField");
        using namespace test::jtx;

        std::string const credIdHex =
            "0011223344556677889900112233445566778899001122334455667788990011";
        uint256 credId;
        BEAST_EXPECT(credId.parseHex(credIdHex));

        Env env{*this};
        OpenView ov{*env.current()};
        STTx const stx = STTx(ttESCROW_FINISH, [&](auto& obj) {
            obj.setAccountID(sfAccount, env.master.id());
            obj.setAccountID(sfOwner, env.master.id());
            obj.setFieldU32(sfOfferSequence, env.seq(env.master));
            obj.setFieldArray(sfMemos, STArray{});
            STVector256 credIds;
            credIds.push_back(credId);
            obj.setFieldV256(sfCredentialIDs, credIds);
        });
        ApplyContext ac = createApplyContext(env, ov, stx);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));

        {
            VirtualRuntime vrt;
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            // hfs.getTxField(sfAccount);
            {
                WasmValVec params(3), result(1);
                auto* trap =
                    ww(getTxField_wrap,
                       &import[6],
                       params,
                       result,
                       sfAccount.getCode(),
                       0,
                       AccountID::bytes);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == AccountID::bytes);
                auto const accountBytes = vrt.getBytes(params, 1);
                BEAST_EXPECT(std::ranges::equal(accountBytes, env.master.id()));
            }

            // hfs.getTxField(sfOwner);
            {
                WasmValVec params(3), result(1);
                auto* trap =
                    ww(getTxField_wrap,
                       &import[6],
                       params,
                       result,
                       sfOwner.getCode(),
                       0,
                       AccountID::bytes);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == AccountID::bytes);
                auto const ownerBytes = vrt.getBytes(params, 1);
                BEAST_EXPECT(std::ranges::equal(ownerBytes, env.master.id()));
            }

            // hfs.getTxField(sfTransactionType);
            {
                WasmValVec params(3), result(1);
                auto* trap =
                    ww(getTxField_wrap,
                       &import[6],
                       params,
                       result,
                       sfTransactionType.getCode(),
                       0,
                       256);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 > 0);
                auto txTypeBytes = vrt.getBytes(params, 1);
                txTypeBytes.resize(result[0].of.i32);
                BEAST_EXPECT(txTypeBytes == toBytes(ttESCROW_FINISH));
            }

            // hfs.getTxField(sfOfferSequence);
            {
                WasmValVec params(3), result(1);
                auto* trap = ww(
                    getTxField_wrap, &import[6], params, result, sfOfferSequence.getCode(), 0, 256);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 > 0);
                auto offerSeqBytes = vrt.getBytes(params, 1);
                offerSeqBytes.resize(result[0].of.i32);
                BEAST_EXPECT(offerSeqBytes == toBytes(env.seq(env.master)));
            }

            // hfs.getTxField(sfDestination);
            {
                WasmValVec params(3), result(1);
                auto* trap = ww(
                    getTxField_wrap, &import[6], params, result, sfDestination.getCode(), 0, 256);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(
                        result[0].of.i32 ==
                        static_cast<int32_t>(HostFunctionError::FIELD_NOT_FOUND));
            }

            // hfs.getTxField(sfMemos);
            {
                WasmValVec params(3), result(1);
                auto* trap =
                    ww(getTxField_wrap, &import[6], params, result, sfMemos.getCode(), 0, 256);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(
                        result[0].of.i32 ==
                        static_cast<int32_t>(HostFunctionError::NOT_LEAF_FIELD));
            }

            // hfs.getTxField(sfCredentialIDs);
            {
                WasmValVec params(3), result(1);
                auto* trap = ww(
                    getTxField_wrap, &import[6], params, result, sfCredentialIDs.getCode(), 0, 256);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
                BEAST_EXPECTS(
                    result[0].of.i32 == static_cast<int32_t>(HostFunctionError::NOT_LEAF_FIELD),
                    std::to_string(result[0].of.i32));
            }

            // hfs.getTxField(sfInvalid);
            {
                WasmValVec params(3), result(1);
                auto* trap =
                    ww(getTxField_wrap, &import[6], params, result, sfInvalid.getCode(), 0, 256);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(
                        result[0].of.i32 ==
                        static_cast<int32_t>(HostFunctionError::FIELD_NOT_FOUND));
            }

            // hfs.getTxField(sfGeneric);
            {
                WasmValVec params(3), result(1);
                auto* trap =
                    ww(getTxField_wrap, &import[6], params, result, sfGeneric.getCode(), 0, 256);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(
                        result[0].of.i32 ==
                        static_cast<int32_t>(HostFunctionError::FIELD_NOT_FOUND));
            }
        }

        {
            auto const iouAsset = env.master["USD"];
            STTx const stx2 = STTx(ttAMM_DEPOSIT, [&](auto& obj) {
                obj.setAccountID(sfAccount, env.master.id());
                obj.setFieldIssue(sfAsset, STIssue{sfAsset, xrpIssue()});
                obj.setFieldIssue(sfAsset2, STIssue{sfAsset2, iouAsset.issue()});
            });
            ApplyContext ac2 = createApplyContext(env, ov, stx2);
            VirtualRuntime vrt;
            WasmHostFunctionsImpl hfs(ac2, dummyEscrow);

            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            // hfs.getTxField(sfAsset);
            {
                WasmValVec params(3), result(1);
                auto* trap =
                    ww(getTxField_wrap, &import[6], params, result, sfAsset.getCode(), 0, 256);

                std::vector<std::uint8_t> const expectedAsset(20, 0);
                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 > 0);
                auto assetBytes = vrt.getBytes(params, 1);
                assetBytes.resize(result[0].of.i32);
                BEAST_EXPECT(assetBytes == expectedAsset);
            }

            // hfs.getTxField(sfAsset2);
            {
                WasmValVec params(3), result(1);
                auto* trap =
                    ww(getTxField_wrap, &import[6], params, result, sfAsset2.getCode(), 0, 256);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 > 0);
                auto asset2Bytes = vrt.getBytes(params, 1);
                asset2Bytes.resize(result[0].of.i32);
                BEAST_EXPECT(asset2Bytes == toBytes(Asset(iouAsset)));
            }
        }

        {
            auto const iouAsset = env.master["GBP"];
            auto const mptId = makeMptID(1, env.master);
            STTx const stx2 = STTx(ttAMM_DEPOSIT, [&](auto& obj) {
                obj.setAccountID(sfAccount, env.master.id());
                obj.setFieldIssue(sfAsset, STIssue{sfAsset, iouAsset.issue()});
                obj.setFieldIssue(sfAsset2, STIssue{sfAsset2, MPTIssue{mptId}});
            });
            ApplyContext ac2 = createApplyContext(env, ov, stx2);
            VirtualRuntime vrt;
            WasmHostFunctionsImpl hfs(ac2, dummyEscrow);

            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            // hfs.getTxField(sfAsset);
            {
                WasmValVec params(3), result(1);
                auto* trap =
                    ww(getTxField_wrap, &import[6], params, result, sfAsset.getCode(), 0, 256);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
                if (BEAST_EXPECT(result[0].of.i32 > 0))
                {
                    auto assetBytes = vrt.getBytes(params, 1);
                    assetBytes.resize(result[0].of.i32);
                    BEAST_EXPECT(assetBytes == toBytes(Asset(iouAsset)));
                }
            }

            // hfs.getTxField(sfAsset2);
            {
                WasmValVec params(3), result(1);
                auto* trap =
                    ww(getTxField_wrap, &import[6], params, result, sfAsset2.getCode(), 0, 256);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
                if (BEAST_EXPECT(result[0].of.i32 > 0))
                {
                    auto assetBytes = vrt.getBytes(params, 1);
                    assetBytes.resize(result[0].of.i32);
                    BEAST_EXPECT(assetBytes == toBytes(Asset(mptId)));
                }
            }
        }

        {
            std::uint8_t const expectedScale = 8;
            STTx const stx2 = STTx(ttMPTOKEN_ISSUANCE_CREATE, [&](auto& obj) {
                obj.setAccountID(sfAccount, env.master.id());
                obj.setFieldU8(sfAssetScale, expectedScale);
            });
            ApplyContext ac2 = createApplyContext(env, ov, stx2);
            VirtualRuntime vrt;
            WasmHostFunctionsImpl hfs(ac2, dummyEscrow);

            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            // hfs.getTxField(sfAssetScale);
            {
                WasmValVec params(3), result(1);
                auto* trap =
                    ww(getTxField_wrap, &import[6], params, result, sfAssetScale.getCode(), 0, 256);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
                if (BEAST_EXPECT(result[0].of.i32 > 0))
                {
                    auto assetBytes = vrt.getBytes(params, 1);
                    assetBytes.resize(result[0].of.i32);
                    BEAST_EXPECT(std::ranges::equal(assetBytes, toBytes(expectedScale)));
                }
            }
        }
    }

    void
    testGetCurrentLedgerObjField()
    {
        testcase("getCurrentLedgerObjField");
        using namespace test::jtx;
        using namespace std::chrono;

        Env env{*this};

        // Fund the account and create an escrow so the ledger object exists
        env(escrow::create(env.master, env.master, XRP(100)), escrow::finish_time(env.now() + 1s));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        // Find the escrow ledger object
        auto const escrowKeylet = keylet::escrow(env.master, env.seq(env.master) - 1);
        BEAST_EXPECT(env.le(escrowKeylet));

        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, escrowKeylet);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // hfs.getCurrentLedgerObjField(sfAccount);
        {
            WasmValVec params(3), result(1);
            auto* trap =
                ww(getCurrentLedgerObjField_wrap,
                   &import[7],
                   params,
                   result,
                   sfAccount.getCode(),
                   0,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECTS(result[0].of.i32 > 0, std::to_string(result[0].of.i32)))
            {
                auto accountBytes = vrt.getBytes(params, 1);
                accountBytes.resize(result[0].of.i32);
                BEAST_EXPECT(std::ranges::equal(accountBytes, env.master.id()));
            }
        }

        // hfs.getCurrentLedgerObjField(sfAmount);
        {
            WasmValVec params(3), result(1);
            auto* trap =
                ww(getCurrentLedgerObjField_wrap,
                   &import[7],
                   params,
                   result,
                   sfAmount.getCode(),
                   0,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECT(result[0].of.i32 > 0))
            {
                auto amountBytes = vrt.getBytes(params, 1);
                amountBytes.resize(result[0].of.i32);
                BEAST_EXPECT(amountBytes == toBytes(XRP(100)));
            }
        }

        // hfs.getCurrentLedgerObjField(sfPreviousTxnID);
        {
            WasmValVec params(3), result(1);
            auto* trap =
                ww(getCurrentLedgerObjField_wrap,
                   &import[7],
                   params,
                   result,
                   sfPreviousTxnID.getCode(),
                   0,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECT(result[0].of.i32 > 0))
            {
                auto previousTxnIdBytes = vrt.getBytes(params, 1);
                previousTxnIdBytes.resize(result[0].of.i32);
                BEAST_EXPECT(previousTxnIdBytes == toBytes(env.tx()->getTransactionID()));
            }
        }

        // hfs.getCurrentLedgerObjField(sfOwner);
        {
            WasmValVec params(3), result(1);
            auto* trap =
                ww(getCurrentLedgerObjField_wrap,
                   &import[7],
                   params,
                   result,
                   sfOwner.getCode(),
                   0,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 == static_cast<int32_t>(HostFunctionError::FIELD_NOT_FOUND));
        }

        {
            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master) + 5);
            VirtualRuntime vrt2;
            WasmHostFunctionsImpl hfs2(ac, dummyEscrow);

            auto import2 = xrpl::createWasmImport(hfs2);
            hfs2.setRT(&vrt2);

            // hfs2.getCurrentLedgerObjField(sfAccount);
            {
                WasmValVec params(3), result(1);
                auto* trap =
                    ww(getCurrentLedgerObjField_wrap,
                       &import2[7],
                       params,
                       result,
                       sfAccount.getCode(),
                       0,
                       256);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(
                        result[0].of.i32 ==
                        static_cast<int32_t>(HostFunctionError::LEDGER_OBJ_NOT_FOUND));
            }
        }
    }

    void
    testGetLedgerObjField()
    {
        testcase("getLedgerObjField");
        using namespace test::jtx;
        using namespace std::chrono;

        Env env{*this};
        // Fund the account and create an escrow so the ledger object exists
        env(escrow::create(env.master, env.master, XRP(100)), escrow::finish_time(env.now() + 1s));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const accountKeylet = keylet::account(env.master.id());
        auto const escrowKeylet = keylet::escrow(env.master.id(), env.seq(env.master) - 1);
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, escrowKeylet);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // hfs.cacheLedgerObj(accountKeylet.key, 1);
        {
            WasmValVec params(3), result(1);
            vrt.setBytes(0, accountKeylet.key.data(), uint256::bytes);
            auto* trap = ww(cacheLedgerObj_wrap, &import[5], params, result, 0, uint256::bytes, 1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 1);
        }

        // hfs.getLedgerObjField(1, sfAccount);
        {
            WasmValVec params(4), result(1);
            auto* trap = ww(
                getLedgerObjField_wrap, &import[8], params, result, 1, sfAccount.getCode(), 0, 256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECTS(result[0].of.i32 > 0, std::to_string(result[0].of.i32)))
            {
                auto accountBytes = vrt.getBytes(params, 2);
                accountBytes.resize(result[0].of.i32);
                BEAST_EXPECT(std::ranges::equal(accountBytes, env.master.id()));
            }
        }

        // hfs.getLedgerObjField(1, sfBalance);
        {
            WasmValVec params(4), result(1);
            auto* trap = ww(
                getLedgerObjField_wrap, &import[8], params, result, 1, sfBalance.getCode(), 0, 256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECT(result[0].of.i32 > 0))
            {
                auto balanceBytes = vrt.getBytes(params, 2);
                balanceBytes.resize(result[0].of.i32);
                BEAST_EXPECT(balanceBytes == toBytes(env.balance(env.master)));
            }
        }

        // hfs.getLedgerObjField(0, sfAccount);
        {
            WasmValVec params(4), result(1);
            auto* trap = ww(
                getLedgerObjField_wrap, &import[8], params, result, 0, sfAccount.getCode(), 0, 256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 == static_cast<int32_t>(HostFunctionError::SLOT_OUT_RANGE));
        }

        // hfs.getLedgerObjField(257, sfAccount);
        {
            WasmValVec params(4), result(1);
            auto* trap =
                ww(getLedgerObjField_wrap,
                   &import[8],
                   params,
                   result,
                   257,
                   sfAccount.getCode(),
                   0,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 == static_cast<int32_t>(HostFunctionError::SLOT_OUT_RANGE));
        }

        // hfs.getLedgerObjField(2, sfAccount);
        {
            WasmValVec params(4), result(1);
            auto* trap = ww(
                getLedgerObjField_wrap, &import[8], params, result, 2, sfAccount.getCode(), 0, 256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 == static_cast<int32_t>(HostFunctionError::EMPTY_SLOT));
        }

        // hfs.getLedgerObjField(1, sfOwner);
        {
            WasmValVec params(4), result(1);
            auto* trap = ww(
                getLedgerObjField_wrap, &import[8], params, result, 1, sfOwner.getCode(), 0, 256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 == static_cast<int32_t>(HostFunctionError::FIELD_NOT_FOUND));
        }
    }

    void
    testGetTxNestedField()
    {
        testcase("getTxNestedField");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};

        std::string const credIdHex =
            "0011223344556677889900112233445566778899001122334455667788990011";
        uint256 credId;
        BEAST_EXPECT(credId.parseHex(credIdHex));

        // Create a transaction with a nested array field
        STTx const stx = STTx(ttESCROW_FINISH, [&](auto& obj) {
            obj.setAccountID(sfAccount, env.master.id());
            STArray memos;
            STObject memoObj(sfMemo);
            memoObj.setFieldVL(sfMemoData, Slice("hello", 5));
            memos.push_back(memoObj);
            obj.setFieldArray(sfMemos, memos);
            STVector256 credIds;
            credIds.push_back(credId);
            obj.setFieldV256(sfCredentialIDs, credIds);
        });

        ApplyContext ac = createApplyContext(env, ov, stx);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // hfs.getTxNestedField(locator);
        {
            // Locator for sfMemos[0].sfMemo.sfMemoData
            // Locator is a sequence of int32_t codes:
            // [sfMemos.fieldCode, 0, sfMemoData.fieldCode]
            std::vector<int32_t> const locatorVec = {sfMemos.fieldCode, 0, sfMemoData.fieldCode};
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));

            WasmValVec params(4), result(1);
            auto* trap =
                ww(getTxNestedField_wrap,
                   &import[9],
                   params,
                   result,
                   0,
                   locatorVec.size() * sizeof(int32_t),
                   256,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECTS(result[0].of.i32 > 0, std::to_string(result[0].of.i32)))
            {
                auto memoDataBytes = vrt.getBytes(params, 2);
                memoDataBytes.resize(result[0].of.i32);
                std::string const memoData(memoDataBytes.begin(), memoDataBytes.end());
                BEAST_EXPECT(memoData == "hello");
            }
        }

        // hfs.getTxNestedField(locator);
        {
            // Locator for sfCredentialIDs[0]
            std::vector<int32_t> locatorVec = {sfCredentialIDs.fieldCode, 0};
            vrt.setBytes(
                0,
                reinterpret_cast<uint8_t const*>(locatorVec.data()),
                locatorVec.size() * sizeof(int32_t));

            WasmValVec params(4), result(1);
            auto* trap =
                ww(getTxNestedField_wrap,
                   &import[9],
                   params,
                   result,
                   0,
                   locatorVec.size() * sizeof(int32_t),
                   256,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECTS(result[0].of.i32 > 0, std::to_string(result[0].of.i32)))
            {
                auto credIdBytes = vrt.getBytes(params, 2);
                credIdBytes.resize(result[0].of.i32);
                std::string const credIdResult(credIdBytes.begin(), credIdBytes.end());
                BEAST_EXPECT(strHex(credIdResult) == credIdHex);
            }
        }

        // hfs.getTxNestedField(locator);
        {
            // can use the nested locator for base fields too
            std::vector<int32_t> locatorVec = {sfAccount.fieldCode};
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));

            WasmValVec params(4), result(1);
            auto* trap =
                ww(getTxNestedField_wrap,
                   &import[9],
                   params,
                   result,
                   0,
                   locatorVec.size() * sizeof(int32_t),
                   256,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECTS(result[0].of.i32 > 0, std::to_string(result[0].of.i32)))
            {
                auto accountBytes = vrt.getBytes(params, 2);
                accountBytes.resize(result[0].of.i32);
                BEAST_EXPECT(std::ranges::equal(accountBytes, env.master.id()));
            }
        }

        // hfs.getTxNestedField(locator);
        {
            // unaligned locator
            std::vector<uint8_t> locatorVec(sizeof(int32_t) + 1);
            memcpy(locatorVec.data() + 1, &sfAccount.fieldCode, sizeof(int32_t));
            vrt.setBytes(0, locatorVec.data() + 1, sizeof(int32_t));

            WasmValVec params(4), result(1);
            auto* trap =
                ww(getTxNestedField_wrap, &import[9], params, result, 0, sizeof(int32_t), 256, 256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECTS(result[0].of.i32 > 0, std::to_string(result[0].of.i32)))
            {
                auto accountBytes = vrt.getBytes(params, 2);
                accountBytes.resize(result[0].of.i32);
                BEAST_EXPECT(std::ranges::equal(accountBytes, env.master.id()));
            }
        }

        auto expectError = [&](std::vector<int32_t> const& locatorVec,
                               HostFunctionError expectedError) {
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));

            WasmValVec params(4), result(1);
            // hfs.getTxNestedField(locator);
            auto* trap =
                ww(getTxNestedField_wrap,
                   &import[9],
                   params,
                   result,
                   0,
                   locatorVec.size() * sizeof(int32_t),
                   256,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECTS(
                result[0].of.i32 == static_cast<int32_t>(expectedError),
                std::to_string(result[0].of.i32));
        };

        // hfs.getTxNestedField(locator);
        // Locator for non-existent base field
        expectError(
            {sfSigners.fieldCode,  // sfSigners does not exist
             0,
             sfAccount.fieldCode},
            HostFunctionError::FIELD_NOT_FOUND);

        // hfs.getTxNestedField(locator);
        // Locator for non-existent index
        expectError(
            {sfMemos.fieldCode,
             1,  // index 1 does not exist
             sfMemoData.fieldCode},
            HostFunctionError::INDEX_OUT_OF_BOUNDS);

        // hfs.getTxNestedField(locator);
        // Locator for non-existent index
        expectError(
            {sfCredentialIDs.fieldCode, 1},  // index 1 does not exist
            HostFunctionError::INDEX_OUT_OF_BOUNDS);

        // hfs.getTxNestedField(locator);
        // Locator for negative index (STArray)
        expectError(
            {sfMemos.fieldCode,
             -1,  // negative index
             sfMemoData.fieldCode},
            HostFunctionError::INDEX_OUT_OF_BOUNDS);

        // hfs.getTxNestedField(locator);
        // Locator for negative index (STVector256)
        expectError(
            {sfCredentialIDs.fieldCode, -1},  // negative index
            HostFunctionError::INDEX_OUT_OF_BOUNDS);

        // hfs.getTxNestedField(locator);
        // Locator for non-existent nested field
        expectError(
            {sfMemos.fieldCode, 0, sfURI.fieldCode},  // sfURI does not exist in the memo
            HostFunctionError::FIELD_NOT_FOUND);

        // hfs.getTxNestedField(locator);
        // Locator for non-existent base sfield
        expectError(
            {field_code(20000, 20000),  // nonexistent SField code
             0,
             sfAccount.fieldCode},
            HostFunctionError::INVALID_FIELD);

        // hfs.getTxNestedField(locator);
        // Locator for non-existent nested sfield
        expectError(
            {sfMemos.fieldCode,  // nonexistent SField code
             0,
             field_code(20000, 20000)},
            HostFunctionError::INVALID_FIELD);

        // hfs.getTxNestedField(locator);
        // Locator for negative base sfield code (-1 = sfInvalid, exists in map but not in tx)
        expectError(
            {-1,  // sfInvalid's field code
             0,
             sfAccount.fieldCode},
            HostFunctionError::FIELD_NOT_FOUND);

        // hfs.getTxNestedField(locator);
        // Locator for zero base sfield code (0 = sfGeneric, exists in map but not in tx)
        expectError(
            {0,  // sfGeneric's field code
             0,
             sfAccount.fieldCode},
            HostFunctionError::FIELD_NOT_FOUND);

        // hfs.getTxNestedField(locator);
        // Locator for very negative base sfield code (not in knownCodeToField map)
        expectError(
            {std::numeric_limits<int32_t>::min(), 0, sfAccount.fieldCode},
            HostFunctionError::INVALID_FIELD);

        // hfs.getTxNestedField(locator);
        // Locator for negative nested sfield code in STObject context
        // (sfMemos[0] is an STObject, then -1 is looked up as SField)
        expectError(
            {sfMemos.fieldCode, 0, -1},  // -1 = sfInvalid, exists in map but not in memo object
            HostFunctionError::FIELD_NOT_FOUND);

        // hfs.getTxNestedField(locator);
        // Locator for STArray
        expectError({sfMemos.fieldCode}, HostFunctionError::NOT_LEAF_FIELD);

        // hfs.getTxNestedField(locator);
        // Locator for STVector256
        expectError({sfCredentialIDs.fieldCode}, HostFunctionError::NOT_LEAF_FIELD);

        // hfs.getTxNestedField(locator);
        // Locator for nesting into non-array/object field
        expectError(
            {sfAccount.fieldCode,  // sfAccount is not an array or object
             0,
             sfAccount.fieldCode},
            HostFunctionError::LOCATOR_MALFORMED);

        // hfs.getTxNestedField(locator);
        // Locator for empty locator
        expectError({}, HostFunctionError::LOCATOR_MALFORMED);

        // hfs.getTxNestedField(locator);
        // Locator for malformed locator (not multiple of 4)
        {
            std::vector<int32_t> locatorVec = {sfMemos.fieldCode};
            vrt.setBytes(0, locatorVec.data(), 3);

            WasmValVec params(4), result(1);
            auto* trap = ww(getTxNestedField_wrap, &import[9], params, result, 0, 3, 256, 256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 == static_cast<int32_t>(HostFunctionError::LOCATOR_MALFORMED));
        }
    }

    void
    testGetCurrentLedgerObjNestedField()
    {
        testcase("getCurrentLedgerObjNestedField");
        using namespace test::jtx;

        Env env{*this};
        Account const alice("alice");
        Account const becky("becky");
        // Create a SignerList for env.master
        env(signers(env.master, 2, {{alice, 1}, {becky, 1}}));

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        // Find the signer ledger object
        auto const signerKeylet = keylet::signers(env.master.id());
        BEAST_EXPECT(env.le(signerKeylet));

        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, signerKeylet);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // hfs.getCurrentLedgerObjNestedField(baseLocatorSlice);
        // Locator for base field
        {
            std::vector<int32_t> baseLocator = {sfSignerQuorum.fieldCode};
            vrt.setBytes(0, baseLocator.data(), baseLocator.size() * sizeof(int32_t));

            WasmValVec params(4), result(1);
            auto* trap =
                ww(getCurrentLedgerObjNestedField_wrap,
                   &import[10],
                   params,
                   result,
                   0,
                   baseLocator.size() * sizeof(int32_t),
                   256,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECTS(result[0].of.i32 > 0, std::to_string(result[0].of.i32)))
            {
                auto signerQuorumBytes = vrt.getBytes(params, 2);
                signerQuorumBytes.resize(result[0].of.i32);
                BEAST_EXPECT(signerQuorumBytes == toBytes(static_cast<uint32_t>(2)));
            }
        }

        auto expectError = [&](std::vector<int32_t> const& locatorVec,
                               HostFunctionError expectedError) {
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));

            WasmValVec params(4), result(1);
            // hfs.getCurrentLedgerObjNestedField(locator);
            auto* trap =
                ww(getCurrentLedgerObjNestedField_wrap,
                   &import[10],
                   params,
                   result,
                   0,
                   locatorVec.size() * sizeof(int32_t),
                   256,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECTS(
                result[0].of.i32 == static_cast<int32_t>(expectedError),
                std::to_string(result[0].of.i32));
        };
        // hfs.getCurrentLedgerObjNestedField(locator);
        // Locator for non-existent base field
        expectError(
            {sfSigners.fieldCode,  // sfSigners does not exist
             0,
             sfAccount.fieldCode},
            HostFunctionError::FIELD_NOT_FOUND);

        // hfs.getCurrentLedgerObjNestedField(locator);
        // Locator for nesting into non-array/object field
        expectError(
            {sfSignerQuorum.fieldCode,  // sfSignerQuorum is not an array or object
             0,
             sfAccount.fieldCode},
            HostFunctionError::LOCATOR_MALFORMED);

        // hfs.getCurrentLedgerObjNestedField(emptyLocator);
        // Locator for empty locator
        {
            WasmValVec params(4), result(1);
            auto* trap = ww(
                getCurrentLedgerObjNestedField_wrap, &import[10], params, result, 0, 0, 256, 256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 == static_cast<int32_t>(HostFunctionError::LOCATOR_MALFORMED));
        }

        // hfs.getCurrentLedgerObjNestedField(malformedLocator);
        // Locator for malformed locator (not multiple of 4)
        {
            std::vector<int32_t> malformedLocatorVec = {sfMemos.fieldCode};
            vrt.setBytes(0, malformedLocatorVec.data(), 3);

            WasmValVec params(4), result(1);
            auto* trap = ww(
                getCurrentLedgerObjNestedField_wrap, &import[10], params, result, 0, 3, 256, 256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 == static_cast<int32_t>(HostFunctionError::LOCATOR_MALFORMED));
        }

        // hfs.getCurrentLedgerObjNestedField(locator);
        {
            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master) + 5);
            VirtualRuntime vrt2;
            WasmHostFunctionsImpl dummyHfs(ac, dummyEscrow);

            auto import2 = xrpl::createWasmImport(dummyHfs);
            dummyHfs.setRT(&vrt2);

            std::vector<int32_t> const locatorVec = {sfAccount.fieldCode};
            vrt2.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));

            WasmValVec params(4), result(1);
            auto* trap =
                ww(getCurrentLedgerObjNestedField_wrap,
                   &import2[10],
                   params,
                   result,
                   0,
                   locatorVec.size() * sizeof(int32_t),
                   256,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECTS(
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::LEDGER_OBJ_NOT_FOUND),
                std::to_string(result[0].of.i32));
        }
    }

    void
    testGetLedgerObjNestedField()
    {
        testcase("getLedgerObjNestedField");
        using namespace test::jtx;

        Env env{*this};
        Account const alice("alice");
        Account const becky("becky");
        // Create a SignerList for env.master
        env(signers(env.master, 2, {{alice, 1}, {becky, 1}}));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // Cache the SignerList ledger object in slot 1
        auto const signerListKeylet = keylet::signers(env.master.id());
        // hfs.cacheLedgerObj(signerListKeylet.key, 1);
        {
            WasmValVec params(3), result(1);
            vrt.setBytes(0, signerListKeylet.key.data(), uint256::bytes);
            auto* trap = ww(cacheLedgerObj_wrap, &import[5], params, result, 0, uint256::bytes, 1);
            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 1);
        }

        // Locator for sfSignerEntries[0].sfAccount
        {
            std::vector<int32_t> const locatorVec = {
                sfSignerEntries.fieldCode, 0, sfAccount.fieldCode};
            // hfs.getLedgerObjNestedField(1, locator);
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));
            WasmValVec params(5), result(1);
            auto* trap =
                ww(getLedgerObjNestedField_wrap,
                   &import[11],
                   params,
                   result,
                   1,
                   0,
                   locatorVec.size() * sizeof(int32_t),
                   256,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECTS(result[0].of.i32 > 0, std::to_string(result[0].of.i32)))
            {
                auto aliceIdBytes = vrt.getBytes(params, 3);
                aliceIdBytes.resize(result[0].of.i32);
                BEAST_EXPECT(std::ranges::equal(aliceIdBytes, alice.id()));
            }
        }

        // Locator for sfSignerEntries[1].sfAccount
        {
            std::vector<int32_t> const locatorVec = {
                sfSignerEntries.fieldCode, 1, sfAccount.fieldCode};
            // hfs.getLedgerObjNestedField(1, locator);
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));
            WasmValVec params(5), result(1);
            auto* trap =
                ww(getLedgerObjNestedField_wrap,
                   &import[11],
                   params,
                   result,
                   1,
                   0,
                   locatorVec.size() * sizeof(int32_t),
                   256,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECTS(result[0].of.i32 > 0, std::to_string(result[0].of.i32)))
            {
                auto beckyIdBytes = vrt.getBytes(params, 3);
                beckyIdBytes.resize(result[0].of.i32);
                BEAST_EXPECT(std::ranges::equal(beckyIdBytes, becky.id()));
            }
        }

        // Locator for sfSignerEntries[0].sfSignerWeight
        {
            std::vector<int32_t> const locatorVec = {
                sfSignerEntries.fieldCode, 0, sfSignerWeight.fieldCode};
            // hfs.getLedgerObjNestedField(1, locator);
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));
            WasmValVec params(5), result(1);
            auto* trap =
                ww(getLedgerObjNestedField_wrap,
                   &import[11],
                   params,
                   result,
                   1,
                   0,
                   locatorVec.size() * sizeof(int32_t),
                   256,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECTS(result[0].of.i32 > 0, std::to_string(result[0].of.i32)))
            {
                // Should be 1
                auto const expected = toBytes(static_cast<std::uint16_t>(1));
                auto weightBytes = vrt.getBytes(params, 3);
                weightBytes.resize(result[0].of.i32);
                BEAST_EXPECT(weightBytes == expected);
            }
        }

        // Locator for base field sfSignerQuorum
        {
            std::vector<int32_t> const locatorVec = {sfSignerQuorum.fieldCode};
            // hfs.getLedgerObjNestedField(1, locator);
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));
            WasmValVec params(5), result(1);
            auto* trap =
                ww(getLedgerObjNestedField_wrap,
                   &import[11],
                   params,
                   result,
                   1,
                   0,
                   locatorVec.size() * sizeof(int32_t),
                   256,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECTS(result[0].of.i32 > 0, std::to_string(result[0].of.i32)))
            {
                auto const expected = toBytes(static_cast<std::uint32_t>(2));
                auto quorumBytes = vrt.getBytes(params, 3);
                quorumBytes.resize(result[0].of.i32);
                BEAST_EXPECT(quorumBytes == expected);
            }
        }

        // Helper for error checks
        auto expectError = [&](std::vector<int32_t> const& locatorVec,
                               HostFunctionError expectedError,
                               int slot = 1) {
            // hfs.getLedgerObjNestedField(slot, locator);
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));
            WasmValVec params(5), result(1);
            auto* trap =
                ww(getLedgerObjNestedField_wrap,
                   &import[11],
                   params,
                   result,
                   slot,
                   0,
                   locatorVec.size() * sizeof(int32_t),
                   256,
                   256);
            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECTS(
                result[0].of.i32 == static_cast<int32_t>(expectedError),
                std::to_string(result[0].of.i32));
        };

        // Error: base field not found
        expectError(
            {sfSigners.fieldCode,  // sfSigners does not exist
             0,
             sfAccount.fieldCode},
            HostFunctionError::FIELD_NOT_FOUND);

        // Error: index out of bounds
        expectError(
            {sfSignerEntries.fieldCode,
             2,  // index 2 does not exist
             sfAccount.fieldCode},
            HostFunctionError::INDEX_OUT_OF_BOUNDS);

        // Error: nested field not found
        expectError(
            {
                sfSignerEntries.fieldCode,
                0,
                sfDestination.fieldCode  // sfDestination does not exist
            },
            HostFunctionError::FIELD_NOT_FOUND);

        // Error: invalid field code
        expectError(
            {field_code(99999, 99999), 0, sfAccount.fieldCode}, HostFunctionError::INVALID_FIELD);

        // Error: invalid nested field code
        expectError(
            {sfSignerEntries.fieldCode, 0, field_code(99999, 99999)},
            HostFunctionError::INVALID_FIELD);

        // Error: slot out of range
        expectError({sfSignerQuorum.fieldCode}, HostFunctionError::SLOT_OUT_RANGE, 0);
        expectError({sfSignerQuorum.fieldCode}, HostFunctionError::SLOT_OUT_RANGE, 257);

        // Error: empty slot
        expectError({sfSignerQuorum.fieldCode}, HostFunctionError::EMPTY_SLOT, 2);

        // Error: locator for STArray (not leaf field)
        expectError({sfSignerEntries.fieldCode}, HostFunctionError::NOT_LEAF_FIELD);

        // Error: nesting into non-array/object field
        expectError(
            {sfSignerQuorum.fieldCode, 0, sfAccount.fieldCode},
            HostFunctionError::LOCATOR_MALFORMED);

        // Error: empty locator
        expectError({}, HostFunctionError::LOCATOR_MALFORMED);

        // Error: locator malformed (not multiple of 4)
        {
            std::vector<int32_t> const locatorVec = {sfSignerEntries.fieldCode};
            // hfs.getLedgerObjNestedField(1, locator);
            vrt.setBytes(0, locatorVec.data(), 3);
            WasmValVec params(5), result(1);
            auto* trap =
                ww(getLedgerObjNestedField_wrap, &import[11], params, result, 1, 0, 3, 256, 256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 == static_cast<int32_t>(HostFunctionError::LOCATOR_MALFORMED));
        }
    }

    void
    testGetTxArrayLen()
    {
        testcase("getTxArrayLen");
        using namespace test::jtx;

        std::string const credIdHex =
            "0011223344556677889900112233445566778899001122334455667788990011";
        uint256 credId;
        BEAST_EXPECT(credId.parseHex(credIdHex));

        Env env{*this};
        OpenView ov{*env.current()};

        // Transaction with an array field
        STTx const stx = STTx(ttESCROW_FINISH, [&](auto& obj) {
            obj.setAccountID(sfAccount, env.master.id());
            STArray memos;
            {
                STObject memoObj(sfMemo);
                memoObj.setFieldVL(sfMemoData, Slice("hello", 5));
                memos.push_back(memoObj);
            }
            {
                STObject memoObj(sfMemo);
                memoObj.setFieldVL(sfMemoData, Slice("world", 5));
                memos.push_back(memoObj);
            }
            obj.setFieldArray(sfMemos, memos);
            STVector256 credIds;
            credIds.push_back(credId);
            obj.setFieldV256(sfCredentialIDs, credIds);
        });

        ApplyContext ac = createApplyContext(env, ov, stx);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // Should return 2 for sfMemos
        // hfs.getTxArrayLen(sfMemos);
        {
            WasmValVec params(1), result(1);
            auto* trap = ww(getTxArrayLen_wrap, &import[12], params, result, sfMemos.fieldCode);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECT(result[0].of.i32 > 0))
                BEAST_EXPECT(result[0].of.i32 == 2);
        }

        // Should return error for non-array field
        // hfs.getTxArrayLen(sfAccount);
        {
            WasmValVec params(1), result(1);
            auto* trap = ww(getTxArrayLen_wrap, &import[12], params, result, sfAccount.fieldCode);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECT(result[0].of.i32 == static_cast<int32_t>(HostFunctionError::NO_ARRAY));
        }

        // Should return error for missing array field
        // hfs.getTxArrayLen(sfSigners);
        {
            WasmValVec params(1), result(1);
            auto* trap = ww(getTxArrayLen_wrap, &import[12], params, result, sfSigners.fieldCode);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECT(
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::FIELD_NOT_FOUND));
        }

        // Should return 1 for sfCredentialIDs
        // hfs.getTxArrayLen(sfCredentialIDs);
        {
            WasmValVec params(1), result(1);
            auto* trap =
                ww(getTxArrayLen_wrap, &import[12], params, result, sfCredentialIDs.fieldCode);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECT(result[0].of.i32 > 0))
                BEAST_EXPECT(result[0].of.i32 == 1);
        }
    }

    void
    testGetCurrentLedgerObjArrayLen()
    {
        testcase("getCurrentLedgerObjArrayLen");
        using namespace test::jtx;

        Env env{*this};
        Account const alice("alice");
        Account const becky("becky");
        // Create a SignerList for env.master
        env(signers(env.master, 2, {{alice, 1}, {becky, 1}}));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const signerKeylet = keylet::signers(env.master.id());
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, signerKeylet);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // hfs.getCurrentLedgerObjArrayLen(sfSignerEntries);
        {
            WasmValVec params(1), result(1);
            auto* trap =
                ww(getCurrentLedgerObjArrayLen_wrap,
                   &import[13],
                   params,
                   result,
                   sfSignerEntries.fieldCode);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECT(result[0].of.i32 > 0))
                BEAST_EXPECT(result[0].of.i32 == 2);
        }

        // hfs.getCurrentLedgerObjArrayLen(sfMemos);
        {
            WasmValVec params(1), result(1);
            auto* trap = ww(
                getCurrentLedgerObjArrayLen_wrap, &import[13], params, result, sfMemos.fieldCode);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECT(
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::FIELD_NOT_FOUND));
        }

        // Should return NO_ARRAY for non-array field
        // hfs.getCurrentLedgerObjArrayLen(sfAccount);
        {
            WasmValVec params(1), result(1);
            auto* trap = ww(
                getCurrentLedgerObjArrayLen_wrap, &import[13], params, result, sfAccount.fieldCode);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECT(result[0].of.i32 == static_cast<int32_t>(HostFunctionError::NO_ARRAY));
        }

        {
            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master) + 5);
            VirtualRuntime vrt2;
            WasmHostFunctionsImpl dummyHfs(ac, dummyEscrow);

            auto import2 = xrpl::createWasmImport(dummyHfs);
            dummyHfs.setRT(&vrt2);

            // auto const len = dummyHfs.getCurrentLedgerObjArrayLen(sfMemos);
            WasmValVec params(1), result(1);
            auto* trap = ww(
                getCurrentLedgerObjArrayLen_wrap, &import2[13], params, result, sfMemos.fieldCode);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECT(
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::LEDGER_OBJ_NOT_FOUND));
        }
    }

    void
    testGetLedgerObjArrayLen()
    {
        testcase("getLedgerObjArrayLen");
        using namespace test::jtx;

        Env env{*this};
        Account const alice("alice");
        Account const becky("becky");
        // Create a SignerList for env.master
        env(signers(env.master, 2, {{alice, 1}, {becky, 1}}));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        auto const signerListKeylet = keylet::signers(env.master.id());
        // hfs.cacheLedgerObj(signerListKeylet.key, 1);
        {
            WasmValVec params(3), result(1);
            vrt.setBytes(0, signerListKeylet.key.data(), uint256::bytes);
            auto* trap = ww(cacheLedgerObj_wrap, &import[5], params, result, 0, uint256::bytes, 1);
            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 1);
        }

        {
            // hfs.getLedgerObjArrayLen(1, sfSignerEntries);
            WasmValVec params(2), result(1);
            auto* trap =
                ww(getLedgerObjArrayLen_wrap,
                   &import[14],
                   params,
                   result,
                   1,
                   sfSignerEntries.fieldCode);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECT(result[0].of.i32 > 0))
            {
                // Should return 2 for sfSignerEntries
                BEAST_EXPECT(result[0].of.i32 == 2);
            }
        }
        {
            // hfs.getLedgerObjArrayLen(0, sfSignerEntries);
            WasmValVec params(2), result(1);
            auto* trap =
                ww(getLedgerObjArrayLen_wrap,
                   &import[14],
                   params,
                   result,
                   0,
                   sfSignerEntries.fieldCode);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECT(
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::SLOT_OUT_RANGE));
        }

        {
            // Should return error for non-array field
            // hfs.getLedgerObjArrayLen(1, sfAccount);
            WasmValVec params(2), result(1);
            auto* trap =
                ww(getLedgerObjArrayLen_wrap, &import[14], params, result, 1, sfAccount.fieldCode);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECT(result[0].of.i32 == static_cast<int32_t>(HostFunctionError::NO_ARRAY));
        }

        {
            // Should return error for empty slot
            // hfs.getLedgerObjArrayLen(2, sfSignerEntries);
            WasmValVec params(2), result(1);
            auto* trap =
                ww(getLedgerObjArrayLen_wrap,
                   &import[14],
                   params,
                   result,
                   2,
                   sfSignerEntries.fieldCode);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECT(result[0].of.i32 == static_cast<int32_t>(HostFunctionError::EMPTY_SLOT));
        }

        {
            // Should return error for missing array field
            // hfs.getLedgerObjArrayLen(1, sfMemos);
            WasmValVec params(2), result(1);
            auto* trap =
                ww(getLedgerObjArrayLen_wrap, &import[14], params, result, 1, sfMemos.fieldCode);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECT(
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::FIELD_NOT_FOUND));
        }
    }

    void
    testGetTxNestedArrayLen()
    {
        testcase("getTxNestedArrayLen");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};

        STTx const stx = STTx(ttESCROW_FINISH, [&](auto& obj) {
            STArray memos;
            STObject memoObj(sfMemo);
            memoObj.setFieldVL(sfMemoData, Slice("hello", 5));
            memos.push_back(memoObj);
            obj.setFieldArray(sfMemos, memos);
        });

        ApplyContext ac = createApplyContext(env, ov, stx);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // Helper for error checks
        auto expectError = [&](std::vector<int32_t> const& locatorVec,
                               HostFunctionError expectedError) {
            // hfs.getTxNestedArrayLen(locator);
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));
            WasmValVec params(2), result(1);
            auto* trap =
                ww(getTxNestedArrayLen_wrap,
                   &import[15],
                   params,
                   result,
                   0,
                   locatorVec.size() * sizeof(int32_t));

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECTS(
                result[0].of.i32 == static_cast<int32_t>(expectedError),
                std::to_string(result[0].of.i32));
        };

        // Locator for sfMemos
        {
            std::vector<int32_t> locatorVec = {sfMemos.fieldCode};
            // hfs.getTxNestedArrayLen(locator);
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));
            WasmValVec params(2), result(1);
            auto* trap =
                ww(getTxNestedArrayLen_wrap,
                   &import[15],
                   params,
                   result,
                   0,
                   locatorVec.size() * sizeof(int32_t));

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECT(result[0].of.i32 == 1);
        }

        // Error: non-array field
        expectError({sfAccount.fieldCode}, HostFunctionError::NO_ARRAY);

        // Error: missing field
        expectError({sfSigners.fieldCode}, HostFunctionError::FIELD_NOT_FOUND);
    }

    void
    testGetCurrentLedgerObjNestedArrayLen()
    {
        testcase("getCurrentLedgerObjNestedArrayLen");
        using namespace test::jtx;

        Env env{*this};
        Account const alice("alice");
        Account const becky("becky");
        // Create a SignerList for env.master
        env(signers(env.master, 2, {{alice, 1}, {becky, 1}}));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const signerKeylet = keylet::signers(env.master.id());
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, signerKeylet);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // Helper for error checks
        auto expectError = [&](std::vector<int32_t> const& locatorVec,
                               HostFunctionError expectedError) {
            // hfs.getCurrentLedgerObjNestedArrayLen(locator);
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));
            WasmValVec params(2), result(1);
            auto* trap =
                ww(getCurrentLedgerObjNestedArrayLen_wrap,
                   &import[16],
                   params,
                   result,
                   0,
                   locatorVec.size() * sizeof(int32_t));

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECTS(
                result[0].of.i32 == static_cast<int32_t>(expectedError),
                std::to_string(result[0].of.i32));
        };

        // Locator for sfSignerEntries
        {
            std::vector<int32_t> locatorVec = {sfSignerEntries.fieldCode};
            // hfs.getCurrentLedgerObjNestedArrayLen(locator);
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));
            WasmValVec params(2), result(1);
            auto* trap =
                ww(getCurrentLedgerObjNestedArrayLen_wrap,
                   &import[16],
                   params,
                   result,
                   0,
                   locatorVec.size() * sizeof(int32_t));

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECT(result[0].of.i32 == 2);
        }

        // Error: non-array field
        expectError({sfSignerQuorum.fieldCode}, HostFunctionError::NO_ARRAY);

        // Error: missing field
        expectError({sfSigners.fieldCode}, HostFunctionError::FIELD_NOT_FOUND);

        {
            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master) + 5);
            VirtualRuntime vrt2;
            WasmHostFunctionsImpl dummyHfs(ac, dummyEscrow);

            auto import2 = xrpl::createWasmImport(dummyHfs);
            dummyHfs.setRT(&vrt2);

            std::vector<int32_t> locatorVec = {sfAccount.fieldCode};
            // auto const result = dummyHfs.getCurrentLedgerObjNestedArrayLen(locator);
            vrt2.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));
            WasmValVec params(2), result(1);
            auto* trap =
                ww(getCurrentLedgerObjNestedArrayLen_wrap,
                   &import2[16],
                   params,
                   result,
                   0,
                   locatorVec.size() * sizeof(int32_t));

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECTS(
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::LEDGER_OBJ_NOT_FOUND),
                std::to_string(result[0].of.i32));
        }
    }

    void
    testGetLedgerObjNestedArrayLen()
    {
        testcase("getLedgerObjNestedArrayLen");
        using namespace test::jtx;

        Env env{*this};
        Account const alice("alice");
        Account const becky("becky");
        env(signers(env.master, 2, {{alice, 1}, {becky, 1}}));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        auto const signerListKeylet = keylet::signers(env.master.id());
        // hfs.cacheLedgerObj(signerListKeylet.key, 1);
        {
            WasmValVec params(3), result(1);
            vrt.setBytes(0, signerListKeylet.key.data(), uint256::bytes);
            auto* trap = ww(cacheLedgerObj_wrap, &import[5], params, result, 0, uint256::bytes, 1);
            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 1);
        }

        // Locator for sfSignerEntries
        std::vector<int32_t> locatorVec = {sfSignerEntries.fieldCode};
        // hfs.getLedgerObjNestedArrayLen(1, locator);
        {
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));
            WasmValVec params(3), result(1);
            auto* trap =
                ww(getLedgerObjNestedArrayLen_wrap,
                   &import[17],
                   params,
                   result,
                   1,
                   0,
                   locatorVec.size() * sizeof(int32_t));

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            if (BEAST_EXPECT(result[0].of.i32 > 0))
                BEAST_EXPECT(result[0].of.i32 == 2);
        }

        // Helper for error checks
        auto expectError = [&](std::vector<int32_t> const& locatorVec,
                               HostFunctionError expectedError,
                               int slot = 1) {
            // hfs.getLedgerObjNestedArrayLen(slot, locator);
            vrt.setBytes(0, locatorVec.data(), locatorVec.size() * sizeof(int32_t));
            WasmValVec params(3), result(1);
            auto* trap =
                ww(getLedgerObjNestedArrayLen_wrap,
                   &import[17],
                   params,
                   result,
                   slot,
                   0,
                   locatorVec.size() * sizeof(int32_t));

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32);
            BEAST_EXPECTS(
                result[0].of.i32 == static_cast<int32_t>(expectedError),
                std::to_string(result[0].of.i32));
        };

        // Error: non-array field
        expectError({sfSignerQuorum.fieldCode}, HostFunctionError::NO_ARRAY);

        // Error: missing field
        expectError({sfSigners.fieldCode}, HostFunctionError::FIELD_NOT_FOUND);

        // Slot out of range
        expectError(locatorVec, HostFunctionError::SLOT_OUT_RANGE, 0);
        expectError(locatorVec, HostFunctionError::SLOT_OUT_RANGE, 257);

        // Empty slot
        expectError(locatorVec, HostFunctionError::EMPTY_SLOT, 2);

        // Error: empty locator
        expectError({}, HostFunctionError::LOCATOR_MALFORMED);

        // Error: locator malformed (not multiple of 4)
        {
            // hfs.getLedgerObjNestedArrayLen(1, malformedLocator);
            vrt.setBytes(0, locatorVec.data(), 3);
            WasmValVec params(3), result(1);
            auto* trap = ww(getLedgerObjNestedArrayLen_wrap, &import[17], params, result, 1, 0, 3);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 == static_cast<int32_t>(HostFunctionError::LOCATOR_MALFORMED));
        }

        // Error: locator for non-STArray field
        expectError(
            {sfSignerQuorum.fieldCode, 0, sfAccount.fieldCode},
            HostFunctionError::LOCATOR_MALFORMED);
    }

    void
    testUpdateData()
    {
        testcase("updateData");
        using namespace test::jtx;

        Env env{*this};
        env(escrow::create(env.master, env.master, XRP(100)),
            escrow::finish_time(env.now() + std::chrono::seconds(1)));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const escrowKeylet = keylet::escrow(env.master, env.seq(env.master) - 1);
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, escrowKeylet);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // Should succeed for small data
        Bytes data(10, 0x42);
        // hfs.updateData(Slice(data.data(), data.size()));
        {
            vrt.setBytes(0, data.data(), data.size());
            WasmValVec params(2), result(1);
            auto* trap = ww(updateData_wrap, &import[67], params, result, 0, data.size());

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == data.size());
            BEAST_EXPECT(hfs.getData() && *hfs.getData() == data);
        }

        // Should fail for too large data
        Bytes bigData(maxWasmDataLength + 1, 0x42);
        // hfs.updateData(Slice(bigData.data(), bigData.size()));
        {
            vrt.setBytes(0, bigData.data(), bigData.size());
            WasmValVec params(2), result(1);
            auto* trap = ww(updateData_wrap, &import[67], params, result, 0, bigData.size());

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 == HfErrorToInt(HostFunctionError::DATA_FIELD_TOO_LARGE));
        }
    }

    void
    testCheckSignature()
    {
        testcase("checkSignature");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // Generate a keypair and sign a message
        auto const kp = generateKeyPair(KeyType::secp256k1, randomSeed());
        PublicKey const& pk = kp.first;
        SecretKey const& sk = kp.second;
        std::string const& message = "hello signature";
        auto const sig = sign(pk, sk, Slice(message.data(), message.size()));

        // Should succeed for valid signature
        {
            // hfs.checkSignature(
            //     Slice(message.data(), message.size()),
            //     Slice(sig.data(), sig.size()),
            //     Slice(pk.data(), pk.size()));
            vrt.setBytes(0, message.data(), message.size());
            vrt.setBytes(256, sig.data(), sig.size());
            vrt.setBytes(512, pk.data(), pk.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(checkSignature_wrap,
                   &import[18],
                   params,
                   result,
                   0,
                   message.size(),
                   256,
                   sig.size(),
                   512,
                   pk.size());

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 1);
        }

        // Should fail for invalid signature
        {
            std::string badSig(sig.size(), 0xFF);
            // hfs.checkSignature(
            //     Slice(message.data(), message.size()),
            //     Slice(badSig.data(), badSig.size()),
            //     Slice(pk.data(), pk.size()));
            vrt.setBytes(0, message.data(), message.size());
            vrt.setBytes(256, badSig.data(), badSig.size());
            vrt.setBytes(512, pk.data(), pk.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(checkSignature_wrap,
                   &import[18],
                   params,
                   result,
                   0,
                   message.size(),
                   256,
                   badSig.size(),
                   512,
                   pk.size());

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 0);
        }

        // Should fail for invalid public key
        {
            std::string badPk(pk.size(), 0x00);
            // hfs.checkSignature(
            //     Slice(message.data(), message.size()),
            //     Slice(sig.data(), sig.size()),
            //     Slice(badPk.data(), badPk.size()));
            vrt.setBytes(0, message.data(), message.size());
            vrt.setBytes(256, sig.data(), sig.size());
            vrt.setBytes(512, badPk.data(), badPk.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(checkSignature_wrap,
                   &import[18],
                   params,
                   result,
                   0,
                   message.size(),
                   256,
                   sig.size(),
                   512,
                   badPk.size());

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == HfErrorToInt(HostFunctionError::INVALID_PARAMS));
        }

        // Should fail for empty public key
        {
            // hfs.checkSignature(
            //     Slice(message.data(), message.size()),
            //     Slice(sig.data(), sig.size()),
            //     Slice(nullptr, 0));
            vrt.setBytes(0, message.data(), message.size());
            vrt.setBytes(256, sig.data(), sig.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(checkSignature_wrap,
                   &import[18],
                   params,
                   result,
                   0,
                   message.size(),
                   256,
                   sig.size(),
                   512,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == HfErrorToInt(HostFunctionError::INVALID_PARAMS));
        }

        // Should fail for empty signature
        {
            // hfs.checkSignature(
            //     Slice(message.data(), message.size()),
            //     Slice(nullptr, 0),
            //     Slice(pk.data(), pk.size()));
            vrt.setBytes(0, message.data(), message.size());
            vrt.setBytes(512, pk.data(), pk.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(checkSignature_wrap,
                   &import[18],
                   params,
                   result,
                   0,
                   message.size(),
                   256,
                   0,
                   512,
                   pk.size());

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 0);
        }

        // Should fail for empty message
        {
            // hfs.checkSignature(
            //     Slice(nullptr, 0), Slice(sig.data(), sig.size()), Slice(pk.data(), pk.size()));
            vrt.setBytes(256, sig.data(), sig.size());
            vrt.setBytes(512, pk.data(), pk.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(checkSignature_wrap,
                   &import[18],
                   params,
                   result,
                   0,
                   0,
                   256,
                   sig.size(),
                   512,
                   pk.size());

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 0);
        }
    }

    void
    testComputeSha512HalfHash()
    {
        testcase("computeSha512HalfHash");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        std::string data = "hello world";
        // hfs.computeSha512HalfHash(Slice(data.data(), data.size()));
        {
            vrt.setBytes(0, data.data(), data.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(computeSha512HalfHash_wrap,
                   &import[19],
                   params,
                   result,
                   0,
                   data.size(),
                   256,
                   uint256::bytes);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == uint256::bytes);

            // Should match direct call to sha512Half
            auto expected = sha512Half(Slice(data.data(), data.size()));
            auto hashBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(std::ranges::equal(hashBytes, expected));
        }
    }

    void
    testKeyletFunctions()
    {
        testcase("keylet functions");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);
        VirtualRuntime vrt;

        auto const usdIssue = env.master["USD"].issue();
        auto const masterID = env.master.id();
        auto const baseMpt = makeMptID(1, masterID);

        auto imp = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // Lambda to compare a Bytes (std::vector<uint8_t>) to a keylet
        auto compareKeylet = [](std::vector<uint8_t> const& bytes, Keylet const& kl) {
            return std::ranges::equal(bytes, kl.key);
        };

        {
            auto const expected = keylet::account(masterID);
            WasmValVec params(4), result(1);
            auto* trap = ww(accountKeylet_wrap, &imp[20], params, result, masterID, 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 2);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 = ww(accountKeylet_wrap, &imp[20], params, result, xrpAccount(), 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::amm(xrpIssue(), usdIssue);
            WasmValVec params(6), result(1);

            auto* trap =
                ww(ammKeylet_wrap, &imp[21], params, result, xrpIssue(), usdIssue, 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 4);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 =
                ww(ammKeylet_wrap, &imp[21], params, result, xrpIssue(), xrpIssue(), 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_PARAMS));

            auto* trap3 =
                ww(ammKeylet_wrap, &imp[21], params, result, baseMpt, xrpIssue(), 1024, 32);
            BEAST_EXPECT(
                !trap3 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_PARAMS));
        }

        {
            auto const expected = keylet::check(masterID, 1u);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(checkKeylet_wrap, &imp[22], params, result, masterID, toBytes(1u), 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 4);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 =
                ww(checkKeylet_wrap, &imp[22], params, result, xrpAccount(), toBytes(1u), 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        std::string const credTypeStr = "test";
        Slice const credType(credTypeStr.data(), credTypeStr.size());
        Account const alice("alice");
        {
            auto const expected = keylet::credential(masterID, masterID, credType);
            WasmValVec params(8), result(1);
            auto* trap =
                ww(credentialKeylet_wrap,
                   &imp[23],
                   params,
                   result,
                   masterID,
                   masterID,
                   credType,
                   1024,
                   32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 6);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            std::string_view constexpr longCredTypeStr =
                "abcdefghijklmnopqrstuvwxyz01234567890qwertyuiop[]"
                "asdfghjkl;'zxcvbnm8237tr28weufwldebvfv8734t07p";
            Slice const longCredType(longCredTypeStr.data(), longCredTypeStr.size());
            static_assert(longCredTypeStr.size() > maxCredentialTypeLength);
            auto* trap2 =
                ww(credentialKeylet_wrap,
                   &imp[23],
                   params,
                   result,
                   masterID,
                   alice.id(),
                   longCredType,
                   1024,
                   32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_PARAMS));

            auto* trap3 =
                ww(credentialKeylet_wrap,
                   &imp[23],
                   params,
                   result,
                   xrpAccount(),
                   alice.id(),
                   credType,
                   1024,
                   32);
            BEAST_EXPECT(
                !trap3 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));

            auto* trap4 =
                ww(credentialKeylet_wrap,
                   &imp[23],
                   params,
                   result,
                   masterID,
                   xrpAccount(),
                   credType,
                   1024,
                   32);
            BEAST_EXPECT(
                !trap4 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::did(masterID);
            WasmValVec params(4), result(1);
            auto* trap = ww(didKeylet_wrap, &imp[26], params, result, masterID, 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 2);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 = ww(didKeylet_wrap, &imp[26], params, result, xrpAccount(), 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::delegate(masterID, alice.id());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(delegateKeylet_wrap, &imp[24], params, result, masterID, alice.id(), 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 4);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 =
                ww(delegateKeylet_wrap, &imp[24], params, result, masterID, masterID, 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_PARAMS));

            auto* trap3 =
                ww(delegateKeylet_wrap, &imp[24], params, result, masterID, xrpAccount(), 1024, 32);
            BEAST_EXPECT(
                !trap3 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));

            auto* trap4 =
                ww(delegateKeylet_wrap, &imp[24], params, result, xrpAccount(), masterID, 1024, 32);
            BEAST_EXPECT(
                !trap4 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::depositPreauth(masterID, alice.id());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(depositPreauthKeylet_wrap,
                   &imp[25],
                   params,
                   result,
                   masterID,
                   alice.id(),
                   1024,
                   32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 4);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 = ww(
                depositPreauthKeylet_wrap, &imp[25], params, result, masterID, masterID, 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_PARAMS));

            auto* trap3 =
                ww(depositPreauthKeylet_wrap,
                   &imp[25],
                   params,
                   result,
                   masterID,
                   xrpAccount(),
                   1024,
                   32);
            BEAST_EXPECT(
                !trap3 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));

            auto* trap4 =
                ww(depositPreauthKeylet_wrap,
                   &imp[25],
                   params,
                   result,
                   xrpAccount(),
                   masterID,
                   1024,
                   32);
            BEAST_EXPECT(
                !trap4 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::escrow(masterID, 1u);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(escrowKeylet_wrap, &imp[27], params, result, masterID, toBytes(1u), 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 4);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 = ww(
                escrowKeylet_wrap, &imp[27], params, result, xrpAccount(), toBytes(1u), 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        Currency const usd = to_currency("USD");
        {
            auto const expected = keylet::line(masterID, alice.id(), usd);
            WasmValVec params(8), result(1);
            auto* trap =
                ww(lineKeylet_wrap, &imp[28], params, result, masterID, alice.id(), usd, 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 6);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 =
                ww(lineKeylet_wrap, &imp[28], params, result, masterID, masterID, usd, 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_PARAMS));

            auto* trap3 = ww(
                lineKeylet_wrap, &imp[28], params, result, masterID, xrpAccount(), usd, 1024, 32);
            BEAST_EXPECT(
                !trap3 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));

            auto* trap4 = ww(
                lineKeylet_wrap, &imp[28], params, result, xrpAccount(), masterID, usd, 1024, 32);
            BEAST_EXPECT(
                !trap4 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));

            auto* trap5 =
                ww(lineKeylet_wrap,
                   &imp[28],
                   params,
                   result,
                   masterID,
                   alice.id(),
                   to_currency(""),
                   1024,
                   32);
            BEAST_EXPECT(
                !trap5 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_PARAMS));
        }

        {
            auto const expected = keylet::mptIssuance(1u, masterID);
            WasmValVec params(6), result(1);
            auto* trap = ww(
                mptIssuanceKeylet_wrap, &imp[29], params, result, masterID, toBytes(1u), 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 4);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 =
                ww(mptIssuanceKeylet_wrap,
                   &imp[29],
                   params,
                   result,
                   xrpAccount(),
                   toBytes(1u),
                   1024,
                   32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::mptoken(baseMpt, alice.id());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(mptokenKeylet_wrap, &imp[30], params, result, baseMpt, alice.id(), 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 4);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 =
                ww(mptokenKeylet_wrap, &imp[30], params, result, MPTID{}, alice.id(), 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_PARAMS));

            auto* trap3 =
                ww(mptokenKeylet_wrap, &imp[30], params, result, baseMpt, xrpAccount(), 1024, 32);
            BEAST_EXPECT(
                !trap3 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::nftoffer(masterID, 1u);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(nftOfferKeylet_wrap, &imp[31], params, result, masterID, toBytes(1u), 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 4);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 = ww(
                nftOfferKeylet_wrap, &imp[31], params, result, xrpAccount(), toBytes(1u), 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::offer(masterID, 1u);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(offerKeylet_wrap, &imp[32], params, result, masterID, toBytes(1u), 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 4);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 =
                ww(offerKeylet_wrap, &imp[32], params, result, xrpAccount(), toBytes(1u), 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::oracle(masterID, 1u);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(oracleKeylet_wrap, &imp[33], params, result, masterID, toBytes(1u), 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 4);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 = ww(
                oracleKeylet_wrap, &imp[33], params, result, xrpAccount(), toBytes(1u), 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::payChan(masterID, alice.id(), 1u);
            WasmValVec params(8), result(1);
            auto* trap =
                ww(paychanKeylet_wrap,
                   &imp[34],
                   params,
                   result,
                   masterID,
                   alice.id(),
                   toBytes(1u),
                   1024,
                   32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 6);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 =
                ww(paychanKeylet_wrap,
                   &imp[34],
                   params,
                   result,
                   masterID,
                   masterID,
                   toBytes(1u),
                   1024,
                   32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_PARAMS));

            auto* trap3 =
                ww(paychanKeylet_wrap,
                   &imp[34],
                   params,
                   result,
                   masterID,
                   xrpAccount(),
                   toBytes(1u),
                   1024,
                   32);
            BEAST_EXPECT(
                !trap3 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));

            auto* trap4 =
                ww(paychanKeylet_wrap,
                   &imp[34],
                   params,
                   result,
                   xrpAccount(),
                   masterID,
                   toBytes(1u),
                   1024,
                   32);
            BEAST_EXPECT(
                !trap4 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::permissionedDomain(masterID, 1u);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(permissionedDomainKeylet_wrap,
                   &imp[35],
                   params,
                   result,
                   masterID,
                   toBytes(1u),
                   1024,
                   32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 4);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 =
                ww(permissionedDomainKeylet_wrap,
                   &imp[35],
                   params,
                   result,
                   xrpAccount(),
                   toBytes(1u),
                   1024,
                   32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::signers(masterID);
            WasmValVec params(4), result(1);
            auto* trap = ww(signersKeylet_wrap, &imp[36], params, result, masterID, 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 2);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 = ww(signersKeylet_wrap, &imp[36], params, result, xrpAccount(), 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::ticket(masterID, 1u);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(ticketKeylet_wrap, &imp[37], params, result, masterID, toBytes(1u), 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 4);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 = ww(
                ticketKeylet_wrap, &imp[37], params, result, xrpAccount(), toBytes(1u), 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }

        {
            auto const expected = keylet::vault(masterID, 1u);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(vaultKeylet_wrap, &imp[38], params, result, masterID, toBytes(1u), 1024, 32);
            if (BEAST_EXPECT(!trap && result[0].kind == WASM_I32 && result[0].of.i32 == 32))
            {
                auto const actual = vrt.getBytes(params, 4);
                BEAST_EXPECT(compareKeylet(actual, expected));
            }

            auto* trap2 =
                ww(vaultKeylet_wrap, &imp[38], params, result, xrpAccount(), toBytes(1u), 1024, 32);
            BEAST_EXPECT(
                !trap2 && result[0].kind == WASM_I32 &&
                result[0].of.i32 == static_cast<int32_t>(HostFunctionError::INVALID_ACCOUNT));
        }
    }

    void
    testGetNFT()
    {
        testcase("getNFT");
        using namespace test::jtx;

        Env env{*this};
        Account const alice("alice");
        env.fund(XRP(1000), alice);
        env.close();

        // Mint NFT for alice
        uint256 const nftId = token::getNextID(env, alice, 0u, 0u);
        std::string const uri = "https://example.com/nft";
        env(token::mint(alice), token::uri(uri));
        env.close();
        uint256 const nftId2 = token::getNextID(env, alice, 0u, 0u);
        env(token::mint(alice));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow = keylet::escrow(alice, env.seq(alice));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // Should succeed for valid NFT
        {
            // hfs.getNFT(alice.id(), nftId);
            vrt.setBytes(0, alice.id().data(), AccountID::bytes);
            vrt.setBytes(256, nftId.data(), uint256::bytes);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(getNFT_wrap,
                   &import[39],
                   params,
                   result,
                   0,
                   AccountID::bytes,
                   256,
                   uint256::bytes,
                   512,
                   256);

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 > 0))
            {
                auto uriBytes = vrt.getBytes(params, 4);
                uriBytes.resize(result[0].of.i32);
                BEAST_EXPECT(std::ranges::equal(uriBytes, uri));
            }
        }

        // Should fail for invalid account
        {
            // hfs.getNFT(xrpAccount(), nftId);
            vrt.setBytes(0, xrpAccount().data(), AccountID::bytes);
            vrt.setBytes(256, nftId.data(), uint256::bytes);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(getNFT_wrap,
                   &import[39],
                   params,
                   result,
                   0,
                   AccountID::bytes,
                   256,
                   uint256::bytes,
                   512,
                   256);

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32))
                BEAST_EXPECT(result[0].of.i32 == HfErrorToInt(HostFunctionError::INVALID_ACCOUNT));
        }

        // Should fail for invalid nftId
        {
            // hfs.getNFT(alice.id(), uint256());
            uint256 zeroId;
            vrt.setBytes(0, alice.id().data(), AccountID::bytes);
            vrt.setBytes(256, zeroId.data(), uint256::bytes);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(getNFT_wrap,
                   &import[39],
                   params,
                   result,
                   0,
                   AccountID::bytes,
                   256,
                   uint256::bytes,
                   512,
                   256);

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32))
                BEAST_EXPECT(result[0].of.i32 == HfErrorToInt(HostFunctionError::INVALID_PARAMS));
        }

        // Should fail for invalid nftId
        {
            auto const badId = token::getNextID(env, alice, 0u, 1u);
            // hfs.getNFT(alice.id(), badId);
            vrt.setBytes(0, alice.id().data(), AccountID::bytes);
            vrt.setBytes(256, badId.data(), uint256::bytes);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(getNFT_wrap,
                   &import[39],
                   params,
                   result,
                   0,
                   AccountID::bytes,
                   256,
                   uint256::bytes,
                   512,
                   256);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 == HfErrorToInt(HostFunctionError::LEDGER_OBJ_NOT_FOUND));
        }

        {
            // hfs.getNFT(alice.id(), nftId2);
            vrt.setBytes(0, alice.id().data(), AccountID::bytes);
            vrt.setBytes(256, nftId2.data(), uint256::bytes);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(getNFT_wrap,
                   &import[39],
                   params,
                   result,
                   0,
                   AccountID::bytes,
                   256,
                   uint256::bytes,
                   512,
                   256);

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32))
                BEAST_EXPECT(result[0].of.i32 == HfErrorToInt(HostFunctionError::FIELD_NOT_FOUND));
        }
    }

    void
    testGetNFTIssuer()
    {
        testcase("getNFTIssuer");
        using namespace test::jtx;

        Env env{*this};
        // Mint NFT for env.master
        uint32_t const taxon = 12345;
        uint256 const nftId = token::getNextID(env, env.master, taxon);
        env(token::mint(env.master, taxon));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // Should succeed for valid NFT id
        {
            // hfs.getNFTIssuer(nftId);
            vrt.setBytes(0, nftId.data(), uint256::bytes);
            WasmValVec params(4), result(1);
            auto* trap =
                ww(getNFTIssuer_wrap,
                   &import[40],
                   params,
                   result,
                   0,
                   uint256::bytes,
                   256,
                   AccountID::bytes);

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == AccountID::bytes))
            {
                auto issuerBytes = vrt.getBytes(params, 2);
                BEAST_EXPECT(std::ranges::equal(issuerBytes, env.master.id()));
            }
        }

        // Should fail for zero NFT id
        {
            // hfs.getNFTIssuer(uint256());
            uint256 zeroId;
            vrt.setBytes(0, zeroId.data(), uint256::bytes);
            WasmValVec params(4), result(1);
            auto* trap =
                ww(getNFTIssuer_wrap,
                   &import[40],
                   params,
                   result,
                   0,
                   uint256::bytes,
                   256,
                   AccountID::bytes);

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32))
                BEAST_EXPECT(result[0].of.i32 == HfErrorToInt(HostFunctionError::INVALID_PARAMS));
        }
    }

    void
    testGetNFTTaxon()
    {
        testcase("getNFTTaxon");
        using namespace test::jtx;

        Env env{*this};

        uint32_t const taxon = 54321;
        uint256 const nftId = token::getNextID(env, env.master, taxon);
        env(token::mint(env.master, taxon));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // hfs.getNFTTaxon(nftId);
        vrt.setBytes(0, nftId.data(), uint256::bytes);
        WasmValVec params(4), result(1);
        auto* trap =
            ww(getNFTTaxon_wrap,
               &import[41],
               params,
               result,
               0,
               uint256::bytes,
               256,
               sizeof(uint32_t));

        if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
            BEAST_EXPECT(result[0].of.i32 == sizeof(uint32_t)))
        {
            BEAST_EXPECT(vrt.getUint32(params, 2) == taxon);
        }
    }

    void
    testGetNFTFlags()
    {
        testcase("getNFTFlags");
        using namespace test::jtx;

        Env env{*this};

        // Mint NFT with default flags
        uint256 const nftId = token::getNextID(env, env.master, 0u, tfTransferable);
        env(token::mint(env.master, 0), txflags(tfTransferable));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.getNFTFlags(nftId);
            vrt.setBytes(0, nftId.data(), uint256::bytes);
            WasmValVec params(2), result(1);
            auto* trap = ww(getNFTFlags_wrap, &import[42], params, result, 0, uint256::bytes);

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32))
                BEAST_EXPECT(result[0].of.i32 == tfTransferable);
        }

        // Should return 0 for zero NFT id
        {
            // hfs.getNFTFlags(uint256());
            uint256 zeroId;
            vrt.setBytes(0, zeroId.data(), uint256::bytes);
            WasmValVec params(2), result(1);
            auto* trap = ww(getNFTFlags_wrap, &import[42], params, result, 0, uint256::bytes);

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32))
                BEAST_EXPECT(result[0].of.i32 == 0);
        }
    }

    void
    testGetNFTTransferFee()
    {
        testcase("getNFTTransferFee");
        using namespace test::jtx;

        Env env{*this};

        uint16_t const transferFee = 250;
        uint256 const nftId = token::getNextID(env, env.master, 0u, tfTransferable, transferFee);
        env(token::mint(env.master, 0), token::xferFee(transferFee), txflags(tfTransferable));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.getNFTTransferFee(nftId);
            vrt.setBytes(0, nftId.data(), uint256::bytes);
            WasmValVec params(2), result(1);
            auto* trap = ww(getNFTTransferFee_wrap, &import[43], params, result, 0, uint256::bytes);

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32))
                BEAST_EXPECT(result[0].of.i32 == transferFee);
        }

        // Should return 0 for zero NFT id
        {
            // hfs.getNFTTransferFee(uint256());
            uint256 zeroId;
            vrt.setBytes(0, zeroId.data(), uint256::bytes);
            WasmValVec params(2), result(1);
            auto* trap = ww(getNFTTransferFee_wrap, &import[43], params, result, 0, uint256::bytes);

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32))
                BEAST_EXPECT(result[0].of.i32 == 0);
        }
    }

    void
    testGetNFTSerial()
    {
        testcase("getNFTSerial");
        using namespace test::jtx;

        Env env{*this};

        // Mint NFT with serial 0
        uint256 const nftId = token::getNextID(env, env.master, 0u);
        auto const serial = env.seq(env.master);
        env(token::mint(env.master));
        env.close();

        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);

        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.getNFTSerial(nftId);
            vrt.setBytes(0, nftId.data(), uint256::bytes);
            WasmValVec params(4), result(1);
            auto* trap =
                ww(getNFTSerial_wrap,
                   &import[44],
                   params,
                   result,
                   0,
                   uint256::bytes,
                   256,
                   sizeof(uint32_t));

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == sizeof(uint32_t)))
            {
                BEAST_EXPECT(vrt.getUint32(params, 2) == serial);
            }
        }

        // Should return 0 for zero NFT id
        {
            // hfs.getNFTSerial(uint256());
            uint256 zeroId;
            vrt.setBytes(0, zeroId.data(), uint256::bytes);
            WasmValVec params(4), result(1);
            auto* trap =
                ww(getNFTSerial_wrap,
                   &import[44],
                   params,
                   result,
                   0,
                   uint256::bytes,
                   256,
                   sizeof(uint32_t));

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == sizeof(uint32_t)))
            {
                BEAST_EXPECT(vrt.getUint32(params, 2) == 0);
            }
        }
    }

    void
    testTrace()
    {
        testcase("trace");
        using namespace test::jtx;

        {
            Env env(*this);
            OpenView ov{*env.current()};
            test::StreamSink sink{beast::severities::kTrace};
            beast::Journal const jlog{sink};
            ApplyContext ac = createApplyContext(env, ov, jlog);

            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            VirtualRuntime vrt;
            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            std::string const msg = "test trace";
            std::string data = "abc";
            auto const slice = Slice(data.data(), data.size());

            // hfs.trace(msg, slice, false);
            {
                vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
                vrt.setBytes(256, slice.data(), slice.size());
                WasmValVec params(5), result(1);
                auto* trap = ww(
                    trace_wrap, &import[45], params, result, 0, msg.size(), 256, slice.size(), 0);

                if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == 0))
                {
                    auto const messages = sink.messages().str();
                    BEAST_EXPECT(messages.find(msg) != std::string::npos);
                }
            }

            // hfs.trace(msg, slice, true);
            {
                vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
                vrt.setBytes(256, slice.data(), slice.size());
                WasmValVec params(5), result(1);
                auto* trap = ww(
                    trace_wrap, &import[45], params, result, 0, msg.size(), 256, slice.size(), 1);

                if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == 0))
                {
                    auto const messages = sink.messages().str();
                    std::string hex;
                    hex.reserve(data.size() * 2);
                    boost::algorithm::hex(data.begin(), data.end(), std::back_inserter(hex));
                    BEAST_EXPECT(messages.find(msg) != std::string::npos);
                    BEAST_EXPECT(messages.find(hex) != std::string::npos);
                }
            }
        }

        {
            // logs disabled (trace < error)
            Env env(*this);
            OpenView ov{*env.current()};
            test::StreamSink sink{beast::severities::kError};
            beast::Journal const jlog{sink};
            ApplyContext ac = createApplyContext(env, ov, jlog);

            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            VirtualRuntime vrt;
            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            std::string const msg = "test trace";
            std::string data = "abc";
            auto const slice = Slice(data.data(), data.size());

            // hfs.trace(msg, slice, false);
            vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
            vrt.setBytes(256, slice.data(), slice.size());
            WasmValVec params(5), result(1);
            auto* trap =
                ww(trace_wrap, &import[45], params, result, 0, msg.size(), 256, slice.size(), 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 0);
            auto const messages = sink.messages().str();
            BEAST_EXPECT(messages.empty());
        }
    }

    void
    testTraceNum()
    {
        testcase("traceNum");
        using namespace test::jtx;

        {
            Env env(*this);
            OpenView ov{*env.current()};
            test::StreamSink sink{beast::severities::kTrace};
            beast::Journal const jlog{sink};
            ApplyContext ac = createApplyContext(env, ov, jlog);

            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            VirtualRuntime vrt;
            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            std::string const msg = "trace number";
            int64_t const num = 123456789;

            // hfs.traceNum(msg, num);
            vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
            WasmValVec params(3), result(1);
            auto* trap = ww(traceNum_wrap, &import[46], params, result, 0, msg.size(), num);

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 0))
            {
                auto const messages = sink.messages().str();
                BEAST_EXPECT(messages.find(msg) != std::string::npos);
                BEAST_EXPECT(messages.find(std::to_string(num)) != std::string::npos);
            }
        }

        {
            // logs disabled
            Env env(*this);
            OpenView ov{*env.current()};
            test::StreamSink sink{beast::severities::kError};
            beast::Journal const jlog{sink};
            ApplyContext ac = createApplyContext(env, ov, jlog);

            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            VirtualRuntime vrt;
            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            std::string const msg = "trace number";
            int64_t const num = 123456789;

            // hfs.traceNum(msg, num);
            vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
            WasmValVec params(3), result(1);
            auto* trap = ww(traceNum_wrap, &import[46], params, result, 0, msg.size(), num);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 0);
            auto const messages = sink.messages().str();
            BEAST_EXPECT(messages.empty());
        }
    }

    void
    testTraceAccount()
    {
        testcase("traceAccount");
        using namespace test::jtx;

        {
            Env env(*this);
            OpenView ov{*env.current()};
            test::StreamSink sink{beast::severities::kTrace};
            beast::Journal const jlog{sink};
            ApplyContext ac = createApplyContext(env, ov, jlog);

            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            VirtualRuntime vrt;
            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            std::string const msg = "trace account";
            auto const& accountId = env.master.id();

            // hfs.traceAccount(msg, env.master.id());
            vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
            vrt.setBytes(256, accountId.data(), accountId.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(traceAccount_wrap,
                   &import[47],
                   params,
                   result,
                   0,
                   msg.size(),
                   256,
                   accountId.size());

            if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 0))
            {
                auto const messages = sink.messages().str();
                BEAST_EXPECT(messages.find(msg) != std::string::npos);
                BEAST_EXPECT(messages.find(env.master.human()) != std::string::npos);
            }
        }

        {
            // logs disabled
            Env env(*this);
            OpenView ov{*env.current()};
            test::StreamSink sink{beast::severities::kError};
            beast::Journal const jlog{sink};
            ApplyContext ac = createApplyContext(env, ov, jlog);

            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            VirtualRuntime vrt;
            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            std::string msg = "trace account";
            auto const& accountId = env.master.id();

            // hfs.traceAccount(msg, env.master.id());
            vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
            vrt.setBytes(256, accountId.data(), accountId.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(traceAccount_wrap,
                   &import[47],
                   params,
                   result,
                   0,
                   msg.size(),
                   256,
                   accountId.size());

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 0);
            auto const messages = sink.messages().str();
            BEAST_EXPECT(messages.empty());
        }
    }

    void
    testTraceAmount()
    {
        testcase("traceAmount");
        using namespace test::jtx;

        {
            Env env(*this);
            OpenView ov{*env.current()};
            test::StreamSink sink{beast::severities::kTrace};
            beast::Journal const jlog{sink};
            ApplyContext ac = createApplyContext(env, ov, jlog);

            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            VirtualRuntime vrt;
            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            std::string const msg = "trace amount";
            STAmount const amount = XRP(12345);
            {
                // hfs.traceAmount(msg, amount);
                Bytes amountBytes = toBytes(amount);
                vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
                vrt.setBytes(256, amountBytes.data(), amountBytes.size());
                WasmValVec params(4), result(1);
                auto* trap =
                    ww(traceAmount_wrap,
                       &import[49],
                       params,
                       result,
                       0,
                       msg.size(),
                       256,
                       amountBytes.size());

                if (BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == 0))
                {
                    auto const messages = sink.messages().str();
                    BEAST_EXPECT(messages.find(msg) != std::string::npos);
                    BEAST_EXPECT(messages.find(amount.getFullText()) != std::string::npos);
                }
            }

            // IOU amount
            Account const alice("alice");
            env.fund(XRP(1000), alice);
            env.close();
            STAmount const iouAmount = env.master["USD"](100);
            {
                // hfs.traceAmount(msg, iouAmount);
                Bytes amountBytes = toBytes(iouAmount);
                vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
                vrt.setBytes(256, amountBytes.data(), amountBytes.size());
                WasmValVec params(4), result(1);
                auto* trap =
                    ww(traceAmount_wrap,
                       &import[49],
                       params,
                       result,
                       0,
                       msg.size(),
                       256,
                       amountBytes.size());

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == 0);
            }

            // MPT amount
            {
                auto const mptId = makeMptID(42, env.master.id());
                Asset const mptAsset = Asset(mptId);
                STAmount const mptAmount(mptAsset, 123456);

                // hfs.traceAmount(msg, mptAmount);
                Bytes amountBytes = toBytes(mptAmount);
                vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
                vrt.setBytes(256, amountBytes.data(), amountBytes.size());
                WasmValVec params(4), result(1);
                auto* trap =
                    ww(traceAmount_wrap,
                       &import[49],
                       params,
                       result,
                       0,
                       msg.size(),
                       256,
                       amountBytes.size());

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == 0);
            }
        }

        {
            // logs disabled
            Env env(*this);
            OpenView ov{*env.current()};
            test::StreamSink sink{beast::severities::kError};
            beast::Journal const jlog{sink};
            ApplyContext ac = createApplyContext(env, ov, jlog);

            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            VirtualRuntime vrt;
            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            std::string const msg = "trace amount";
            STAmount const amount = XRP(12345);

            // hfs.traceAmount(msg, amount);
            Bytes amountBytes = toBytes(amount);
            vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
            vrt.setBytes(256, amountBytes.data(), amountBytes.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(traceAmount_wrap,
                   &import[49],
                   params,
                   result,
                   0,
                   msg.size(),
                   256,
                   amountBytes.size());

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 0);
            auto const messages = sink.messages().str();
            BEAST_EXPECT(messages.empty());
        }
    }

    // clang-format off

    int const normalExp = 18;

    Bytes const floatIntMin        =  {0xF3, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x00, 0x00, 0x00, 0x01};  // -2^63
    Bytes const floatIntZero       =  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00};  // 0
    Bytes const floatIntMax        =  {0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00};  // 2^63-1
    Bytes const floatUIntMax       =  {0x19, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9A, 0x00, 0x00, 0x00, 0x01};  // 2^64-1

    Bytes const floatMaxExp        =  {0x0D, 0xE0, 0xB6, 0xB3, 0xA7, 0x64, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00};  // 1e(Number::maxExponent + normalExp)
    Bytes const floatPreMaxExp     =  {0x0D, 0xE0, 0xB6, 0xB3, 0xA7, 0x64, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xFF};  // 1e(Number::maxExponent + normalExp - 1)
    Bytes const floatMinusMaxExp   =  {0xF2, 0x1F, 0x49, 0x4C, 0x58, 0x9C, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00};  // -1e(Number::maxExponent + normalExp)
    Bytes const floatMinExp        =  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00};  // 1e(Number::minExponent - normalExp)
    Bytes const floatMax           =  {0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x80, 0x00};  // Number::maxRep e(Number::maxExponent - normalExp)

    Bytes const floatMaxIOU        =  {0x0D, 0xE0, 0xB6, 0xB3, 0xA7, 0x63, 0xFF, 0x9C, 0x00, 0x00, 0x00, 0x4E};  // 9999999999999999e(96)
    Bytes const floatMinIOU        =  {0x0D, 0xE0, 0xB6, 0xB3, 0xA7, 0x64, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x9D};  // 1e(-96 - 3 + normalExp = -81)

    Bytes const float1             =  {0x0D, 0xE0, 0xB6, 0xB3, 0xA7, 0x64, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xEE};  // 1
    Bytes const floatMinus1        =  {0xF2, 0x1F, 0x49, 0x4C, 0x58, 0x9C, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xEE};  // -1
    Bytes const float1More         =  {0x0D, 0xE0, 0xB6, 0xB3, 0xA7, 0x64, 0x03, 0xE8, 0xFF, 0xFF, 0xFF, 0xEE};  // 1.000 000 000 000 001
    Bytes const float2             =  {0x1B, 0xC1, 0x6D, 0x67, 0x4E, 0xC8, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xEE};  // 2
    Bytes const float10            =  {0x0D, 0xE0, 0xB6, 0xB3, 0xA7, 0x64, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xEF};  // 10
    Bytes const floatPi            =  {0x2B, 0x99, 0x2D, 0xDF, 0xA2, 0x32, 0x48, 0xE8, 0xFF, 0xFF, 0xFF, 0xEE};  // 3.141592653589793
    Bytes const floatInvalidZero   =  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x00, 0x00, 0x00};  // INVALID
    Bytes const floatMinus3        =  {0xD6, 0x5D, 0xDB, 0xE5, 0x09, 0xD4, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xEE};  // -3

    std::string const invalid = "invalid_data";

    // clang-format on

    template <class T>
    void
    printFloats(std::string_view descr, T m, int e)
    {
        Serializer msg;
        Number n;

        if constexpr (std::is_signed_v<T>)
        {
            n = Number(static_cast<int64_t>(m), e);
        }
        else
        {
            n = Number(static_cast<uint64_t>(m), e, Number::normalized());
        }

        STNumber(sfNumber, n).add(msg);
        auto const& data = msg.modData();
        std::cout << std::setw(24) << descr << " m: " << std::setw(20) << n.mantissa()
                  << ", e: " << std::setw(8) << n.exponent() << ", hex: ";
        std::cout << std::hex << std::uppercase << std::setfill('0');
        for (auto const& c : data)
            std::cout << std::setw(2) << (unsigned)c << " ";
        std::cout << std::dec << std::setfill(' ') << std::endl;
    }

    void
    printNumbersBin()
    {
        printFloats("int64.min", std::numeric_limits<int64_t>::min(), 0);
        printFloats("zero", 0, 0);
        printFloats("int64.max", std::numeric_limits<int64_t>::max(), 0);
        printFloats("uint64.max", std::numeric_limits<uint64_t>::max(), 0);

        printFloats("Number 1 max exp", 1, Number::maxExponent + normalExp);
        printFloats("Number (max exp - 1)", 1, Number::maxExponent + normalExp - 1);
        printFloats("Number -1 max exp", -1, Number::maxExponent + normalExp);

        printFloats("Number.max", Number::maxRep, Number::maxExponent);
        printFloats("Number min positive", 1, Number::minExponent + normalExp);
        printFloats(
            "Number.min", std::numeric_limits<int64_t>::min(), Number::maxExponent - normalExp);
        printFloats("STAmount.max", STAmount::cMaxValue, STAmount::cMaxOffset);
        printFloats("STAmount min positive", STAmount::cMinValue, STAmount::cMinOffset);

        printFloats("one", 1, 0);
        printFloats("-one", -1, 0);
        printFloats("1,00...01", 1'000'000'000'000'001, -15);
        printFloats("two", 2, 0);
        printFloats("ten", 10, 0);
        printFloats("pi", 3141592653589793, -15);
        printFloats("-three", -3, 0);
        return;
    }

    void
    testTraceFloat()
    {
        testcase("traceFloat");
        using namespace test::jtx;

        {
            Env env{*this};
            OpenView ov{*env.current()};
            ApplyContext ac = createApplyContext(env, ov);

            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            VirtualRuntime vrt;
            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            std::string const msg = "trace float";

            {
                // hfs.traceFloat(msg, makeSlice(invalid));
                vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
                vrt.setBytes(256, reinterpret_cast<uint8_t const*>(invalid.data()), invalid.size());
                WasmValVec params(4), result(1);
                auto* trap =
                    ww(traceFloat_wrap,
                       &import[48],
                       params,
                       result,
                       0,
                       msg.size(),
                       256,
                       invalid.size());

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == 0);
            }

            {
                // hfs.traceFloat(msg, makeSlice(floatMaxExp));
                vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
                vrt.setBytes(256, floatMaxExp.data(), floatMaxExp.size());
                WasmValVec params(4), result(1);
                auto* trap =
                    ww(traceFloat_wrap,
                       &import[48],
                       params,
                       result,
                       0,
                       msg.size(),
                       256,
                       floatMaxExp.size());

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == 0);
            }
        }

        {
            // logs disabled
            Env env(*this);
            OpenView ov{*env.current()};
            test::StreamSink sink{beast::severities::kError};
            beast::Journal const jlog{sink};
            ApplyContext ac = createApplyContext(env, ov, jlog);

            auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            VirtualRuntime vrt;
            auto import = xrpl::createWasmImport(hfs);
            hfs.setRT(&vrt);

            std::string const msg = "trace float";

            // hfs.traceFloat(msg, makeSlice(invalid));
            vrt.setBytes(0, reinterpret_cast<uint8_t const*>(msg.data()), msg.size());
            vrt.setBytes(256, reinterpret_cast<uint8_t const*>(invalid.data()), invalid.size());
            WasmValVec params(4), result(1);
            auto* trap = ww(
                traceFloat_wrap, &import[48], params, result, 0, msg.size(), 256, invalid.size());

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 0);
            auto const messages = sink.messages().str();
            BEAST_EXPECT(messages.empty());
        }
    }

    void
    testFloatFromInt()
    {
        testcase("floatFromInt");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.floatFromInt(min64, -1);
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatFromInt_wrap, &import[50], params, result, min64, 0, FLOAT_SIZE, -1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatFromInt(min64, 4);
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatFromInt_wrap, &import[50], params, result, min64, 0, FLOAT_SIZE, 4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatFromInt(min64, 0);
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatFromInt_wrap, &import[50], params, result, min64, 0, FLOAT_SIZE, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 1);
            BEAST_EXPECT(resultBytes == floatIntMin);
        }

        {
            // hfs.floatFromInt(0, 0);
            WasmValVec params(4), result(1);
            auto* trap = ww(floatFromInt_wrap, &import[50], params, result, 0ll, 0, FLOAT_SIZE, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 1);
            BEAST_EXPECT(resultBytes == floatIntZero);
        }

        {
            // hfs.floatFromInt(max64, 0);
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatFromInt_wrap, &import[50], params, result, max64, 0, FLOAT_SIZE, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 1);
            BEAST_EXPECT(resultBytes == floatIntMax);
        }
    }

    void
    testFloatFromUint()
    {
        testcase("floatFromUint");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.floatFromUint(std::numeric_limits<uint64_t>::min(), -1);
            WasmValVec params(5), result(1);
            uint64_t val = std::numeric_limits<uint64_t>::min();
            vrt.setBytes(0, &val, sizeof(val));
            auto* trap =
                ww(floatFromUint_wrap, &import[51], params, result, 0, 8, 16, FLOAT_SIZE, -1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatFromUint(std::numeric_limits<uint64_t>::min(), 4);
            WasmValVec params(5), result(1);
            uint64_t val = std::numeric_limits<uint64_t>::min();
            vrt.setBytes(0, &val, sizeof(val));
            auto* trap =
                ww(floatFromUint_wrap, &import[51], params, result, 0, 8, 16, FLOAT_SIZE, 4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatFromUint(0, 0);
            WasmValVec params(5), result(1);
            uint64_t val = 0;
            vrt.setBytes(0, &val, sizeof(val));
            auto* trap =
                ww(floatFromUint_wrap, &import[51], params, result, 0, 8, 16, FLOAT_SIZE, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatIntZero);
        }

        {
            // hfs.floatFromUint(std::numeric_limits<uint64_t>::max(), 0);
            WasmValVec params(5), result(1);
            uint64_t val = std::numeric_limits<uint64_t>::max();
            vrt.setBytes(0, &val, sizeof(val));
            auto* trap =
                ww(floatFromUint_wrap, &import[51], params, result, 0, 8, 16, FLOAT_SIZE, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatUIntMax);
        }
    }

    void
    testFloatSet()
    {
        testcase("floatSet");
        using namespace test::jtx;
        using namespace wasm_float;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.floatSet(1, 0, -1);
            WasmValVec params(5), result(1);
            auto* trap = ww(floatSet_wrap, &import[58], params, result, 0, 1ll, 0, FLOAT_SIZE, -1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatSet(1, 0, 4);
            WasmValVec params(5), result(1);
            auto* trap = ww(floatSet_wrap, &import[58], params, result, 0, 1ll, 0, FLOAT_SIZE, 4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatSet(1, Number::maxExponent + normalExp + 1, 0);
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatSet_wrap,
                   &import[58],
                   params,
                   result,
                   Number::maxExponent + normalExp + 1,
                   1ll,
                   0,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatSet(1, Number::minExponent + normalExp - 1, 0);
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatSet_wrap,
                   &import[58],
                   params,
                   result,
                   Number::minExponent + normalExp - 1,
                   1ll,
                   0,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatIntZero);
        }

        {
            // hfs.floatSet(1, Number::maxExponent + normalExp, 0);
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatSet_wrap,
                   &import[58],
                   params,
                   result,
                   Number::maxExponent + normalExp,
                   1ll,
                   0,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatMaxExp);
        }

        {
            // hfs.floatSet(-1, Number::maxExponent + normalExp, 0);
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatSet_wrap,
                   &import[58],
                   params,
                   result,
                   Number::maxExponent + normalExp,
                   -1ll,
                   0,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatMinusMaxExp);
        }

        {
            // hfs.floatSet(1, Number::maxExponent + normalExp - 1, 0);
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatSet_wrap,
                   &import[58],
                   params,
                   result,
                   Number::maxExponent + normalExp - 1,
                   1ll,
                   0,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatPreMaxExp);
        }

        {
            // hfs.floatSet(STAmount::cMaxValue, STAmount::cMaxOffset, 0);
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatSet_wrap,
                   &import[58],
                   params,
                   result,
                   STAmount::cMaxOffset,
                   static_cast<int64_t>(STAmount::cMaxValue),
                   0,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatMaxIOU);
        }

        {
            // hfs.floatSet(1, Number::minExponent + normalExp, 0);
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatSet_wrap,
                   &import[58],
                   params,
                   result,
                   Number::minExponent - normalExp,
                   1ll,
                   0,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatMinExp);
        }

        {
            // hfs.floatSet(10, -1, 0);
            WasmValVec params(5), result(1);
            auto* trap = ww(floatSet_wrap, &import[58], params, result, -1, 10ll, 0, FLOAT_SIZE, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == float1);
        }

        {
            // hfs.floatSet(1, Number::maxExponent + normalExp + 1, 0);
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatSet_wrap,
                   &import[58],
                   params,
                   result,
                   Number::maxExponent + normalExp + 1,
                   1ll,
                   0,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }
    }

    void
    testFloatCompare()
    {
        testcase("floatCompare");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.floatCompare(Slice(), Slice());
            WasmValVec params(4), result(1);
            auto* trap = ww(floatCompare_wrap, &import[59], params, result, 0, 0, 0, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatCompare(makeSlice(floatInvalidZero), Slice());
            WasmValVec params(4), result(1);
            vrt.setBytes(0, floatInvalidZero.data(), floatInvalidZero.size());
            auto* trap = ww(floatCompare_wrap, &import[59], params, result, 0, FLOAT_SIZE, 0, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatCompare(makeSlice(float1), makeSlice(invalid));
            WasmValVec params(4), result(1);
            vrt.setBytes(0, float1.data(), float1.size());
            vrt.setBytes(FLOAT_SIZE, invalid.data(), invalid.size());
            auto* trap =
                ww(floatCompare_wrap,
                   &import[59],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   invalid.size());

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatCompare(makeSlice(floatIntMin), makeSlice(floatIntZero));
            WasmValVec params(4), result(1);
            vrt.setBytes(0, floatIntMin.data(), floatIntMin.size());
            vrt.setBytes(FLOAT_SIZE, floatIntZero.data(), floatIntZero.size());
            auto* trap =
                ww(floatCompare_wrap,
                   &import[59],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 2);
        }

        {
            // hfs.floatCompare(makeSlice(floatIntMax), makeSlice(floatIntZero));
            WasmValVec params(4), result(1);
            vrt.setBytes(0, floatIntMax.data(), floatIntMax.size());
            vrt.setBytes(FLOAT_SIZE, floatIntZero.data(), floatIntZero.size());
            auto* trap =
                ww(floatCompare_wrap,
                   &import[59],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 1);
        }

        {
            // hfs.floatCompare(makeSlice(float1), makeSlice(float1));
            WasmValVec params(4), result(1);
            vrt.setBytes(0, float1.data(), float1.size());
            vrt.setBytes(FLOAT_SIZE, float1.data(), float1.size());
            auto* trap =
                ww(floatCompare_wrap,
                   &import[59],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 0);
        }
    }

    void
    testFloatAdd()
    {
        testcase("floatAdd");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.floatAdd(Slice(), Slice(), -1);
            WasmValVec params(7), result(1);
            auto* trap =
                ww(floatAdd_wrap, &import[60], params, result, 0, 0, 0, 0, 0, FLOAT_SIZE, -1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatAdd(Slice(), Slice(), 0);
            WasmValVec params(7), result(1);
            auto* trap =
                ww(floatAdd_wrap, &import[60], params, result, 0, 0, 0, 0, 0, FLOAT_SIZE, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatAdd(makeSlice(float1), makeSlice(invalid), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, float1.data(), float1.size());
            vrt.setBytes(FLOAT_SIZE, invalid.data(), invalid.size());
            auto* trap =
                ww(floatAdd_wrap,
                   &import[60],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   invalid.size(),
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatAdd(makeSlice(floatMaxIOU), makeSlice(floatMaxExp), 0);
            // max IOU is too small to make any change
            WasmValVec params(7), result(1);
            vrt.setBytes(0, floatMaxIOU.data(), floatMaxIOU.size());
            vrt.setBytes(FLOAT_SIZE, floatMaxExp.data(), floatMaxExp.size());
            auto* trap =
                ww(floatAdd_wrap,
                   &import[60],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 4);
            BEAST_EXPECT(resultBytes == floatMaxExp);
        }

        {
            // hfs.floatAdd(makeSlice(floatIntMin), makeSlice(floatIntZero), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, floatIntMin.data(), floatIntMin.size());
            vrt.setBytes(FLOAT_SIZE, floatIntZero.data(), floatIntZero.size());
            auto* trap =
                ww(floatAdd_wrap,
                   &import[60],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 4);
            BEAST_EXPECT(resultBytes == floatIntMin);
        }

        {
            // hfs.floatAdd(makeSlice(floatIntMax), makeSlice(floatIntMin), 0);//
            //  Number can't hold int64.min, it is rounded and we get -3, not -1
            WasmValVec params(7), result(1);
            vrt.setBytes(0, floatIntMax.data(), floatIntMax.size());
            vrt.setBytes(FLOAT_SIZE, floatIntMin.data(), floatIntMin.size());
            auto* trap =
                ww(floatAdd_wrap,
                   &import[60],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 4);
            BEAST_EXPECT(resultBytes == floatMinus3);
        }
    }

    void
    testFloatSubtract()
    {
        testcase("floatSubtract");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.floatSubtract(Slice(), Slice(), -1);
            WasmValVec params(7), result(1);
            auto* trap =
                ww(floatSubtract_wrap, &import[61], params, result, 0, 0, 0, 0, 0, FLOAT_SIZE, -1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatSubtract(Slice(), Slice(), 0);
            WasmValVec params(7), result(1);
            auto* trap =
                ww(floatSubtract_wrap, &import[61], params, result, 0, 0, 0, 0, 0, FLOAT_SIZE, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatSubtract(makeSlice(float1), makeSlice(invalid), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, float1.data(), float1.size());
            vrt.setBytes(FLOAT_SIZE, invalid.data(), invalid.size());
            auto* trap =
                ww(floatSubtract_wrap,
                   &import[61],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   invalid.size(),
                   FLOAT_SIZE * 2,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatSubtract(makeSlice(floatMinusMaxExp), makeSlice(floatMaxIOU), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, floatMinusMaxExp.data(), floatMinusMaxExp.size());
            vrt.setBytes(FLOAT_SIZE, floatMaxIOU.data(), floatMaxIOU.size());
            auto* trap =
                ww(floatSubtract_wrap,
                   &import[61],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 4);
            BEAST_EXPECT(resultBytes == floatMinusMaxExp);
        }

        {
            // hfs.floatSubtract(makeSlice(floatIntMin), makeSlice(floatIntZero), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, floatIntMin.data(), floatIntMin.size());
            vrt.setBytes(FLOAT_SIZE, floatIntZero.data(), floatIntZero.size());
            auto* trap =
                ww(floatSubtract_wrap,
                   &import[61],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 4);
            BEAST_EXPECT(resultBytes == floatIntMin);
        }

        {
            // hfs.floatSubtract(makeSlice(floatIntZero), makeSlice(float1), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, floatIntZero.data(), floatIntZero.size());
            vrt.setBytes(FLOAT_SIZE, float1.data(), float1.size());
            auto* trap =
                ww(floatSubtract_wrap,
                   &import[61],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 4);
            BEAST_EXPECT(resultBytes == floatMinus1);
        }
    }

    void
    testFloatMultiply()
    {
        testcase("floatMultiply");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.floatMultiply(Slice(), Slice(), -1);
            WasmValVec params(7), result(1);
            auto* trap =
                ww(floatMultiply_wrap, &import[62], params, result, 0, 0, 0, 0, 0, FLOAT_SIZE, -1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatMultiply(Slice(), Slice(), 0);
            WasmValVec params(7), result(1);
            auto* trap =
                ww(floatMultiply_wrap, &import[62], params, result, 0, 0, 0, 0, 0, FLOAT_SIZE, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatMultiply(makeSlice(float1), makeSlice(invalid), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, float1.data(), float1.size());
            vrt.setBytes(FLOAT_SIZE, invalid.data(), invalid.size());
            auto* trap =
                ww(floatMultiply_wrap,
                   &import[62],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   invalid.size(),
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatMultiply(makeSlice(floatMax), makeSlice(float1More), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, floatMax.data(), floatMax.size());
            vrt.setBytes(FLOAT_SIZE, float1More.data(), float1More.size());
            auto* trap =
                ww(floatMultiply_wrap,
                   &import[62],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_COMPUTATION_ERROR));
        }

        {
            // hfs.floatMultiply(makeSlice(float1), makeSlice(float1), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, float1.data(), float1.size());
            vrt.setBytes(FLOAT_SIZE, float1.data(), float1.size());
            auto* trap =
                ww(floatMultiply_wrap,
                   &import[62],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 4);
            BEAST_EXPECT(resultBytes == float1);
        }

        {
            // hfs.floatMultiply(makeSlice(floatIntZero), makeSlice(floatMaxIOU), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, floatIntZero.data(), floatIntZero.size());
            vrt.setBytes(FLOAT_SIZE, floatMaxIOU.data(), floatMaxIOU.size());
            auto* trap =
                ww(floatMultiply_wrap,
                   &import[62],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 4);
            BEAST_EXPECT(resultBytes == floatIntZero);
        }

        {
            // hfs.floatMultiply(makeSlice(float10), makeSlice(floatPreMaxExp), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, float10.data(), float10.size());
            vrt.setBytes(FLOAT_SIZE, floatPreMaxExp.data(), floatPreMaxExp.size());
            auto* trap =
                ww(floatMultiply_wrap,
                   &import[62],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 4);
            BEAST_EXPECT(resultBytes == floatMaxExp);
        }
    }

    void
    testFloatDivide()
    {
        testcase("floatDivide");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.floatDivide(Slice(), Slice(), -1);
            WasmValVec params(7), result(1);
            auto* trap =
                ww(floatDivide_wrap, &import[63], params, result, 0, 0, 0, 0, 0, FLOAT_SIZE, -1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatDivide(Slice(), Slice(), 0);
            WasmValVec params(7), result(1);
            auto* trap =
                ww(floatDivide_wrap, &import[63], params, result, 0, 0, 0, 0, 0, FLOAT_SIZE, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {  // hfs.floatDivide(makeSlice(float1), makeSlice(invalid), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, float1.data(), float1.size());
            vrt.setBytes(FLOAT_SIZE, invalid.data(), invalid.size());
            auto* trap =
                ww(floatDivide_wrap,
                   &import[63],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   invalid.size(),
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {  // hfs.floatDivide(makeSlice(float1), makeSlice(floatIntZero), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, float1.data(), float1.size());
            vrt.setBytes(FLOAT_SIZE, floatIntZero.data(), floatIntZero.size());
            auto* trap =
                ww(floatDivide_wrap,
                   &import[63],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_COMPUTATION_ERROR));
        }

        {  // hfs.floatDivide(makeSlice(floatMax), makeSlice(*y), 0);
            auto const y = hfs.floatSet(STAmount::cMaxValue, -normalExp - 1, 0);  // 0.9999999...
            if (BEAST_EXPECT(y))
            {
                WasmValVec params(7), result(1);
                vrt.setBytes(0, floatMax.data(), floatMax.size());
                vrt.setBytes(FLOAT_SIZE, y->data(), y->size());
                auto* trap =
                    ww(floatDivide_wrap,
                       &import[63],
                       params,
                       result,
                       0,
                       FLOAT_SIZE,
                       FLOAT_SIZE,
                       FLOAT_SIZE,
                       2 * FLOAT_SIZE,
                       FLOAT_SIZE,
                       0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(
                        result[0].of.i32 ==
                        static_cast<int32_t>(HostFunctionError::FLOAT_COMPUTATION_ERROR));
            }
        }

        {  // hfs.floatDivide(makeSlice(floatIntZero), makeSlice(float1), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, floatIntZero.data(), floatIntZero.size());
            vrt.setBytes(FLOAT_SIZE, float1.data(), float1.size());
            auto* trap =
                ww(floatDivide_wrap,
                   &import[63],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 4);
            BEAST_EXPECT(resultBytes == floatIntZero);
        }

        {  // hfs.floatDivide(makeSlice(floatMaxExp), makeSlice(float10), 0);
            WasmValVec params(7), result(1);
            vrt.setBytes(0, floatMaxExp.data(), floatMaxExp.size());
            vrt.setBytes(FLOAT_SIZE, float10.data(), float10.size());
            auto* trap =
                ww(floatDivide_wrap,
                   &import[63],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 4);
            BEAST_EXPECT(resultBytes == floatPreMaxExp);
        }
    }

    void
    testFloatRoot()
    {
        testcase("floatRoot");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {  // hfs.floatRoot(Slice(), 2, -1);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(floatRoot_wrap, &import[64], params, result, 0, 0, 2, 0, FLOAT_SIZE, -1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {  // hfs.floatRoot(makeSlice(invalid), 3, 0);
            WasmValVec params(6), result(1);
            vrt.setBytes(0, invalid.data(), invalid.size());
            auto* trap =
                ww(floatRoot_wrap,
                   &import[64],
                   params,
                   result,
                   0,
                   invalid.size(),
                   3,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {  // hfs.floatRoot(makeSlice(float1), -2, 0);
            WasmValVec params(6), result(1);
            vrt.setBytes(0, float1.data(), float1.size());
            auto* trap =
                ww(floatRoot_wrap,
                   &import[64],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   -2,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {  // hfs.floatRoot(makeSlice(floatIntZero), 2, 0);
            WasmValVec params(6), result(1);
            vrt.setBytes(0, floatIntZero.data(), floatIntZero.size());
            auto* trap =
                ww(floatRoot_wrap,
                   &import[64],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   2,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 3);
            BEAST_EXPECT(resultBytes == floatIntZero);
        }

        {  // hfs.floatRoot(makeSlice(floatMaxIOU), 1, 0);
            WasmValVec params(6), result(1);
            vrt.setBytes(0, floatMaxIOU.data(), floatMaxIOU.size());
            auto* trap =
                ww(floatRoot_wrap,
                   &import[64],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   1,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 3);
            BEAST_EXPECT(resultBytes == floatMaxIOU);
        }

        {
            // hfs.floatRoot(makeSlice(*x), 2, 0);
            auto const x = hfs.floatSet(100, 0, 0);  // 100
            if (BEAST_EXPECT(x))
            {
                WasmValVec params(6), result(1);
                vrt.setBytes(0, x->data(), x->size());
                auto* trap =
                    ww(floatRoot_wrap,
                       &import[64],
                       params,
                       result,
                       0,
                       FLOAT_SIZE,
                       2,
                       2 * FLOAT_SIZE,
                       FLOAT_SIZE,
                       0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
                auto const resultBytes = vrt.getBytes(params, 3);
                BEAST_EXPECT(resultBytes == float10);
            }
        }

        {
            // hfs.floatRoot(makeSlice(*x), 3, 0);
            auto const x = hfs.floatSet(1000, 0, 0);  // 1000
            if (BEAST_EXPECT(x))
            {
                WasmValVec params(6), result(1);
                vrt.setBytes(0, x->data(), x->size());
                auto* trap =
                    ww(floatRoot_wrap,
                       &import[64],
                       params,
                       result,
                       0,
                       FLOAT_SIZE,
                       3,
                       2 * FLOAT_SIZE,
                       FLOAT_SIZE,
                       0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
                auto const resultBytes = vrt.getBytes(params, 3);
                BEAST_EXPECT(resultBytes == float10);
            }
        }

        {
            // hfs.floatRoot(makeSlice(*x), 2, 0);
            auto const x = hfs.floatSet(1, -2, 0);  // 0.01
            auto const y = hfs.floatSet(1, -1, 0);  // 0.1
            if (BEAST_EXPECT(x && y))
            {
                WasmValVec params(6), result(1);
                vrt.setBytes(0, x->data(), x->size());
                auto* trap =
                    ww(floatRoot_wrap,
                       &import[64],
                       params,
                       result,
                       0,
                       FLOAT_SIZE,
                       2,
                       2 * FLOAT_SIZE,
                       FLOAT_SIZE,
                       0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
                auto const resultBytes = vrt.getBytes(params, 3);
                BEAST_EXPECT(resultBytes == *y);
            }
        }
    }

    void
    testFloatPower()
    {
        testcase("floatPower");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {  // hfs.floatPower(Slice(), 2, -1);
            WasmValVec params(6), result(1);
            auto* trap =
                ww(floatPower_wrap, &import[65], params, result, 0, 0, 2, 0, FLOAT_SIZE, -1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {  // hfs.floatPower(makeSlice(invalid), 3, 0);
            WasmValVec params(6), result(1);
            vrt.setBytes(0, invalid.data(), invalid.size());
            auto* trap =
                ww(floatPower_wrap,
                   &import[65],
                   params,
                   result,
                   0,
                   invalid.size(),
                   3,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {  // hfs.floatPower(makeSlice(float1), -2, 0);
            WasmValVec params(6), result(1);
            vrt.setBytes(0, float1.data(), float1.size());
            auto* trap =
                ww(floatPower_wrap,
                   &import[65],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   -2,
                   2 * FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatPower(makeSlice(floatMax), 2, 0);
            WasmValVec params(6), result(1);
            vrt.setBytes(0, floatMax.data(), floatMax.size());
            auto* trap =
                ww(floatPower_wrap,
                   &import[65],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   2,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_COMPUTATION_ERROR));
        }

        {
            // hfs.floatPower(makeSlice(floatMax), Number::maxExponent + 1, 0);
            WasmValVec params(6), result(1);
            vrt.setBytes(0, floatMax.data(), floatMax.size());
            auto* trap =
                ww(floatPower_wrap,
                   &import[65],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   Number::maxExponent + 1,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatPower(makeSlice(floatMaxIOU), 0, 0);
            WasmValVec params(6), result(1);
            vrt.setBytes(0, floatMaxIOU.data(), floatMaxIOU.size());
            auto* trap =
                ww(floatPower_wrap,
                   &import[65],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   0,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 3);
            BEAST_EXPECT(resultBytes == float1);
        }

        {  // hfs.floatPower(makeSlice(floatMaxIOU), 1, 0);
            WasmValVec params(6), result(1);
            vrt.setBytes(0, floatMaxIOU.data(), floatMaxIOU.size());
            auto* trap =
                ww(floatPower_wrap,
                   &import[65],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   1,
                   FLOAT_SIZE,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 3);
            BEAST_EXPECT(resultBytes == floatMaxIOU);
        }

        {
            // hfs.floatPower(makeSlice(float10), 2, 0);
            auto const x = hfs.floatSet(100, 0, 0);  // 100
            if (BEAST_EXPECT(x))
            {
                WasmValVec params(6), result(1);
                vrt.setBytes(0, float10.data(), float10.size());
                auto* trap =
                    ww(floatPower_wrap,
                       &import[65],
                       params,
                       result,
                       0,
                       FLOAT_SIZE,
                       2,
                       2 * FLOAT_SIZE,
                       FLOAT_SIZE,
                       0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
                auto const resultBytes = vrt.getBytes(params, 3);
                BEAST_EXPECT(resultBytes == *x);
            }
        }

        {                                           // hfs.floatPower(makeSlice(*x), 2, 0);
            auto const x = hfs.floatSet(1, -1, 0);  // 0.1
            auto const y = hfs.floatSet(1, -2, 0);  // 0.01
            if (BEAST_EXPECT(x && y))
            {
                WasmValVec params(6), result(1);
                vrt.setBytes(0, x->data(), x->size());
                auto* trap =
                    ww(floatPower_wrap,
                       &import[65],
                       params,
                       result,
                       0,
                       FLOAT_SIZE,
                       2,
                       2 * FLOAT_SIZE,
                       FLOAT_SIZE,
                       0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
                auto const resultBytes = vrt.getBytes(params, 3);
                BEAST_EXPECT(resultBytes == *y);
            }
        }
    }

    void
    testFloatLog()
    {
        testcase("floatLog");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {  // hfs.floatLog(Slice(), -1);
            WasmValVec params(5), result(1);
            auto* trap = ww(floatLog_wrap, &import[66], params, result, 0, 0, 0, FLOAT_SIZE, -1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            auto const x = hfs.floatSet(32'786, 0, 0);
            if (BEAST_EXPECT(x))
            {
                WasmValVec params(5), result(1);
                vrt.setBytes(0, floatMaxExp.data(), floatMaxExp.size());
                auto* trap =
                    ww(floatLog_wrap,
                       &import[66],
                       params,
                       result,
                       0,
                       FLOAT_SIZE,
                       2 * FLOAT_SIZE,
                       FLOAT_SIZE,
                       0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
                auto const resultBytes = vrt.getBytes(params, 2);
                BEAST_EXPECT(resultBytes == *x);
            }
        }

        {
            // hfs.floatLog(makeSlice(*x), 0);
            auto const x = hfs.floatSet(100, 0, 0);  // 100
            if (BEAST_EXPECT(x))
            {
                WasmValVec params(5), result(1);
                vrt.setBytes(0, x->data(), x->size());
                auto* trap =
                    ww(floatLog_wrap,
                       &import[66],
                       params,
                       result,
                       0,
                       FLOAT_SIZE,
                       2 * FLOAT_SIZE,
                       FLOAT_SIZE,
                       0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
                auto const resultBytes = vrt.getBytes(params, 2);
                BEAST_EXPECT(resultBytes == float2);
            }
        }

        {
            // hfs.floatLog(makeSlice(*x), 0);
            auto const x = hfs.floatSet(1000, 0, 0);  // 1000
            auto const y = hfs.floatSet(3, 0, 0);     // 3
            if (BEAST_EXPECT(x && y))
            {
                WasmValVec params(5), result(1);
                vrt.setBytes(0, x->data(), x->size());
                auto* trap =
                    ww(floatLog_wrap,
                       &import[66],
                       params,
                       result,
                       0,
                       FLOAT_SIZE,
                       2 * FLOAT_SIZE,
                       FLOAT_SIZE,
                       0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
                auto const resultBytes = vrt.getBytes(params, 2);
                BEAST_EXPECT(resultBytes == *y);
            }
        }

        {
            // hfs.floatLog(makeSlice(*x), 0);
            auto const x = hfs.floatSet(1, -2, 0);                                   // 0.01
            auto const y = hfs.floatSet(-1'999'999'999'999'999'999, -normalExp, 0);  // -2
            if (BEAST_EXPECT(x && y))
            {
                WasmValVec params(5), result(1);
                vrt.setBytes(0, x->data(), x->size());
                auto* trap =
                    ww(floatLog_wrap,
                       &import[66],
                       params,
                       result,
                       0,
                       FLOAT_SIZE,
                       2 * FLOAT_SIZE,
                       FLOAT_SIZE,
                       0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
                auto const resultBytes = vrt.getBytes(params, 2);
                BEAST_EXPECT(resultBytes == *y);
            }
        }
    }

    void
    testFloatSpecialCases()
    {
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        WasmHostFunctionsImpl const hfs(ac, dummyEscrow);

        testcase("float non-canonical");

        {  // non-canonical mantissa 100000e-4
            Bytes const y = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x86, 0xA0, 0xFF, 0xFF, 0xFF, 0xFC};
            auto const result = hfs.floatCompare(makeSlice(y), makeSlice(float10));
            BEAST_EXPECT(result && *result == 0);
        }
    }

    void
    testFloatFromSTAmount()
    {
        testcase("floatFromSTAmount");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.floatFromSTAmount(amount, -1);
            STAmount const amount = XRP(100);
            Bytes amountBytes = toBytes(amount);
            vrt.setBytes(0, amountBytes.data(), amountBytes.size());
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatFromSTAmount_wrap,
                   &import[52],
                   params,
                   result,
                   0,
                   amountBytes.size(),
                   256,
                   FLOAT_SIZE,
                   -1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatFromSTAmount(amount, 4);
            STAmount const amount = XRP(100);
            Bytes amountBytes = toBytes(amount);
            vrt.setBytes(0, amountBytes.data(), amountBytes.size());
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatFromSTAmount_wrap,
                   &import[52],
                   params,
                   result,
                   0,
                   amountBytes.size(),
                   256,
                   FLOAT_SIZE,
                   4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatFromSTAmount(amount, 0);
            STAmount const amount = XRP(0);
            Bytes amountBytes = toBytes(amount);
            vrt.setBytes(0, amountBytes.data(), amountBytes.size());
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatFromSTAmount_wrap,
                   &import[52],
                   params,
                   result,
                   0,
                   amountBytes.size(),
                   256,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatIntZero);
        }

        {
            // hfs.floatFromSTAmount(amount, 0);
            STAmount const amount = XRP(-1);
            auto const y = hfs.floatSet(-1 * 1'000'000, 0, 0);
            if (BEAST_EXPECT(y))
            {
                Bytes amountBytes = toBytes(amount);
                vrt.setBytes(0, amountBytes.data(), amountBytes.size());
                WasmValVec params(5), result(1);
                auto* trap =
                    ww(floatFromSTAmount_wrap,
                       &import[52],
                       params,
                       result,
                       0,
                       amountBytes.size(),
                       256,
                       FLOAT_SIZE,
                       0);

                BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                    BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
                auto const resultBytes = vrt.getBytes(params, 2);
                BEAST_EXPECT(resultBytes == *y);
            }
        }

        {
            // hfs.floatFromSTAmount(amount, 0);
            auto const y = hfs.floatSet(9223372036854776, 3, 0);
            STAmount const amount(noIssue(), std::numeric_limits<int64_t>::max());
            Bytes amountBytes = toBytes(amount);
            vrt.setBytes(0, amountBytes.data(), amountBytes.size());
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatFromSTAmount_wrap,
                   &import[52],
                   params,
                   result,
                   0,
                   amountBytes.size(),
                   256,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == *y);
        }

        {
            bool ex = false;
            try
            {
                STAmount const amount(noIssue(), -1, Number::maxExponent + normalExp);
                [[maybe_unused]] Bytes const amountBytes = toBytes(amount);
            }
            catch (...)
            {
                ex = true;
            }

            BEAST_EXPECT(ex);
        }

        auto const USD = env.master["USD"];
        {
            // hfs.floatFromSTAmount(amount, 0);
            STAmount const amount(
                IOUAmount(STAmount::cMinValue, STAmount::cMinOffset), USD.issue());
            Bytes amountBytes = toBytes(amount);
            vrt.setBytes(0, amountBytes.data(), amountBytes.size());
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatFromSTAmount_wrap,
                   &import[52],
                   params,
                   result,
                   0,
                   amountBytes.size(),
                   256,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatMinIOU);
        }

        {
            // hfs.floatFromSTAmount(amount, 0);
            STAmount const amount(
                IOUAmount(STAmount::cMaxValue, STAmount::cMaxOffset), USD.issue());
            Bytes amountBytes = toBytes(amount);
            vrt.setBytes(0, amountBytes.data(), amountBytes.size());
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatFromSTAmount_wrap,
                   &import[52],
                   params,
                   result,
                   0,
                   amountBytes.size(),
                   256,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatMaxIOU);
        }
    }

    void
    testFloatFromSTNumber()
    {
        testcase("floatFromSTNumber");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        // Test with invalid rounding mode
        {
            // hfs.floatFromSTNumber(num, -1);
            STNumber const num(sfNumber, Number(123, 0));
            Bytes numBytes = toBytes(num);
            vrt.setBytes(0, numBytes.data(), numBytes.size());
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatFromSTNumber_wrap,
                   &import[53],
                   params,
                   result,
                   0,
                   numBytes.size(),
                   256,
                   FLOAT_SIZE,
                   -1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatFromSTNumber(num, 4);
            STNumber const num(sfNumber, Number(123, 0));
            Bytes numBytes = toBytes(num);
            vrt.setBytes(0, numBytes.data(), numBytes.size());
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatFromSTNumber_wrap,
                   &import[53],
                   params,
                   result,
                   0,
                   numBytes.size(),
                   256,
                   FLOAT_SIZE,
                   4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatFromSTNumber(num, 0);
            STNumber const num(
                sfNumber, Number(std::numeric_limits<uint64_t>::max(), 0, Number::normalized()));
            Bytes numBytes = toBytes(num);
            vrt.setBytes(0, numBytes.data(), numBytes.size());
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatFromSTNumber_wrap,
                   &import[53],
                   params,
                   result,
                   0,
                   numBytes.size(),
                   256,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatUIntMax);
        }

        {
            // hfs.floatFromSTNumber(num, 0);
            STNumber const num(sfNumber, Number(-1, Number::maxExponent + normalExp));
            Bytes numBytes = toBytes(num);
            vrt.setBytes(0, numBytes.data(), numBytes.size());
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatFromSTNumber_wrap,
                   &import[53],
                   params,
                   result,
                   0,
                   numBytes.size(),
                   256,
                   FLOAT_SIZE,
                   0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatMinusMaxExp);
        }
    }

    void
    testFloatToInt()
    {
        testcase("floatToInt");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.floatToInt(makeSlice(float1), -1);
            vrt.setBytes(0, float1.data(), float1.size());
            WasmValVec params(5), result(1);
            auto* trap =
                ww(floatToInt_wrap, &import[54], params, result, 0, FLOAT_SIZE, 256, 8, -1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatToInt(makeSlice(float1), 4);
            vrt.setBytes(0, float1.data(), float1.size());
            WasmValVec params(5), result(1);
            auto* trap = ww(floatToInt_wrap, &import[54], params, result, 0, FLOAT_SIZE, 256, 8, 4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatToInt(Slice(), 0);
            WasmValVec params(5), result(1);
            auto* trap = ww(floatToInt_wrap, &import[54], params, result, 0, 0, 256, 8, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatToInt(makeSlice(invalid), 0);
            vrt.setBytes(0, invalid.data(), invalid.size());
            WasmValVec params(5), result(1);
            auto* trap = ww(floatToInt_wrap, &import[54], params, result, 0, FLOAT_SIZE, 256, 8, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatToInt(makeSlice(floatIntZero), 0);
            vrt.setBytes(0, floatIntZero.data(), floatIntZero.size());
            WasmValVec params(5), result(1);
            auto* trap = ww(floatToInt_wrap, &import[54], params, result, 0, FLOAT_SIZE, 256, 8, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 8);
            auto const resultVal = vrt.getInt64(params, 2);
            BEAST_EXPECT(resultVal == 0);

            // roundtrip
            auto const result2 = hfs.floatFromInt(resultVal, 0);
            BEAST_EXPECT(result2) && BEAST_EXPECT(*result2 == floatIntZero);
        }

        {
            // hfs.floatToInt(makeSlice(float1), 0);
            vrt.setBytes(0, float1.data(), float1.size());
            WasmValVec params(5), result(1);
            auto* trap = ww(floatToInt_wrap, &import[54], params, result, 0, FLOAT_SIZE, 256, 8, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 8);
            auto const resultVal = vrt.getInt64(params, 2);
            BEAST_EXPECT(resultVal == 1);

            // roundtrip
            auto const result2 = hfs.floatFromInt(resultVal, 0);
            BEAST_EXPECT(result2) && BEAST_EXPECT(*result2 == float1);
        }

        {
            // hfs.floatToInt(makeSlice(floatMinus1), 0);
            vrt.setBytes(0, floatMinus1.data(), floatMinus1.size());
            WasmValVec params(5), result(1);
            auto* trap = ww(floatToInt_wrap, &import[54], params, result, 0, FLOAT_SIZE, 256, 8, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 8);
            auto const resultVal = vrt.getInt64(params, 2);
            BEAST_EXPECT(resultVal == -1);

            // roundtrip
            auto const result2 = hfs.floatFromInt(resultVal, 0);
            BEAST_EXPECT(result2) && BEAST_EXPECT(*result2 == floatMinus1);
        }

        {
            // hfs.floatToInt(makeSlice(floatIntMax), 0);
            vrt.setBytes(0, floatIntMax.data(), floatIntMax.size());
            WasmValVec params(5), result(1);
            auto* trap = ww(floatToInt_wrap, &import[54], params, result, 0, FLOAT_SIZE, 256, 8, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 8);
            auto const resultVal = vrt.getInt64(params, 2);
            BEAST_EXPECT(resultVal == std::numeric_limits<int64_t>::max());

            // roundtrip
            auto const result2 = hfs.floatFromInt(resultVal, 0);
            BEAST_EXPECT(result2) && BEAST_EXPECT(*result2 == floatIntMax);
        }

        {
            // Number can't hold int64.min, it is rounded and we get int64_t.min - 3, which doesn't
            // fit into int64
            // hfs.floatToInt(makeSlice(floatIntMin), 0);
            vrt.setBytes(0, floatIntMin.data(), floatIntMin.size());
            WasmValVec params(5), result(1);
            auto* trap = ww(floatToInt_wrap, &import[54], params, result, 0, FLOAT_SIZE, 256, 8, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_COMPUTATION_ERROR));
        }

        {
            // hfs.floatToInt(makeSlice(floatUIntMax), 0);
            vrt.setBytes(0, floatUIntMax.data(), floatUIntMax.size());
            WasmValVec params(5), result(1);
            auto* trap = ww(floatToInt_wrap, &import[54], params, result, 0, FLOAT_SIZE, 256, 8, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_COMPUTATION_ERROR));
        }

        // Test rounding modes with pi (3.141592653589793)
        {
            // to_nearest (mode 0): should round to 3
            // hfs.floatToInt(makeSlice(floatPi), 0);
            vrt.setBytes(0, floatPi.data(), floatPi.size());
            WasmValVec params(5), result(1);
            auto* trap = ww(floatToInt_wrap, &import[54], params, result, 0, FLOAT_SIZE, 256, 8, 0);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 8);
            auto const resultVal = vrt.getInt64(params, 2);
            BEAST_EXPECT(resultVal == 3);
        }

        {
            // towards_zero (mode 1): should truncate to 3
            // hfs.floatToInt(makeSlice(floatPi), 1);
            vrt.setBytes(0, floatPi.data(), floatPi.size());
            WasmValVec params(5), result(1);
            auto* trap = ww(floatToInt_wrap, &import[54], params, result, 0, FLOAT_SIZE, 256, 8, 1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 8);
            auto const resultVal = vrt.getInt64(params, 2);
            BEAST_EXPECT(resultVal == 3);
        }

        {
            // downward (mode 2): should round down to 3
            // hfs.floatToInt(makeSlice(floatPi), 2);
            vrt.setBytes(0, floatPi.data(), floatPi.size());
            WasmValVec params(5), result(1);
            auto* trap = ww(floatToInt_wrap, &import[54], params, result, 0, FLOAT_SIZE, 256, 8, 2);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 8);
            auto const resultVal = vrt.getInt64(params, 2);
            BEAST_EXPECT(resultVal == 3);
        }

        {
            // upward (mode 3): should round up to 4
            // hfs.floatToInt(makeSlice(floatPi), 3);
            vrt.setBytes(0, floatPi.data(), floatPi.size());
            WasmValVec params(5), result(1);
            auto* trap = ww(floatToInt_wrap, &import[54], params, result, 0, FLOAT_SIZE, 256, 8, 3);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == 8);
            auto const resultVal = vrt.getInt64(params, 2);
            BEAST_EXPECT(resultVal == 4);
        }
    }

    void
    testFloatToMantissaAndExponent()
    {
        testcase("floatToMantissaAndExponent");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.floatToMantissaAndExponent(makeSlice(invalid));
            vrt.setBytes(0, invalid.data(), invalid.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(floatToMantissaAndExponent_wrap,
                   &import[55],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   256,
                   8,
                   512,
                   4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatToMantissaAndExponent(makeSlice(floatIntZero));
            vrt.setBytes(0, floatIntZero.data(), floatIntZero.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(floatToMantissaAndExponent_wrap,
                   &import[55],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   256,
                   8,
                   512,
                   4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const mantissa = vrt.getInt64(params, 2);
            auto const exponent = vrt.getInt32(params, 4);
            BEAST_EXPECT(mantissa == 0) &&
                BEAST_EXPECT(exponent == std::numeric_limits<int32_t>::min());

            // roundtrip
            auto const result2 = hfs.floatSet(mantissa, exponent, 0);
            BEAST_EXPECT(result2) && BEAST_EXPECT(*result2 == floatIntZero);
        }

        {
            // hfs.floatToMantissaAndExponent(makeSlice(float1));
            vrt.setBytes(0, float1.data(), float1.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(floatToMantissaAndExponent_wrap,
                   &import[55],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   256,
                   8,
                   512,
                   4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const mantissa = vrt.getInt64(params, 2);
            auto const exponent = vrt.getInt32(params, 4);
            BEAST_EXPECT(mantissa == 1000000000000000000) && BEAST_EXPECT(exponent == -normalExp);

            // roundtrip
            auto const result2 = hfs.floatSet(mantissa, exponent, 0);
            BEAST_EXPECT(result2) && BEAST_EXPECT(*result2 == float1);
        }

        {
            // hfs.floatToMantissaAndExponent(makeSlice(floatMinus1));
            vrt.setBytes(0, floatMinus1.data(), floatMinus1.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(floatToMantissaAndExponent_wrap,
                   &import[55],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   256,
                   8,
                   512,
                   4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const mantissa = vrt.getInt64(params, 2);
            auto const exponent = vrt.getInt32(params, 4);
            BEAST_EXPECT(mantissa == -1000000000000000000) && BEAST_EXPECT(exponent == -normalExp);

            // roundtrip
            auto const result2 = hfs.floatSet(mantissa, exponent, 0);
            BEAST_EXPECT(result2) && BEAST_EXPECT(*result2 == floatMinus1);
        }

        {
            // hfs.floatToMantissaAndExponent(makeSlice(float10));
            vrt.setBytes(0, float10.data(), float10.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(floatToMantissaAndExponent_wrap,
                   &import[55],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   256,
                   8,
                   512,
                   4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const mantissa = vrt.getInt64(params, 2);
            auto const exponent = vrt.getInt32(params, 4);
            BEAST_EXPECT(mantissa == 1000000000000000000) &&
                BEAST_EXPECT(exponent == -normalExp + 1);

            // roundtrip
            auto const result2 = hfs.floatSet(mantissa, exponent, 0);
            BEAST_EXPECT(result2) && BEAST_EXPECT(*result2 == float10);
        }

        {
            // hfs.floatToMantissaAndExponent(makeSlice(floatPi));
            vrt.setBytes(0, floatPi.data(), floatPi.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(floatToMantissaAndExponent_wrap,
                   &import[55],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   256,
                   8,
                   512,
                   4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const mantissa = vrt.getInt64(params, 2);
            auto const exponent = vrt.getInt32(params, 4);
            BEAST_EXPECT(mantissa == 3141592653589793000) && BEAST_EXPECT(exponent == -normalExp);

            // roundtrip
            auto const result2 = hfs.floatSet(mantissa, exponent, 0);
            BEAST_EXPECT(result2) && BEAST_EXPECT(*result2 == floatPi);
        }

        {
            // hfs.floatToMantissaAndExponent(makeSlice(floatIntMax));
            vrt.setBytes(0, floatIntMax.data(), floatIntMax.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(floatToMantissaAndExponent_wrap,
                   &import[55],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   256,
                   8,
                   512,
                   4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const mantissa = vrt.getInt64(params, 2);
            auto const exponent = vrt.getInt32(params, 4);
            BEAST_EXPECT(mantissa == std::numeric_limits<int64_t>::max()) &&
                BEAST_EXPECT(exponent == 0);

            // roundtrip
            auto const result2 = hfs.floatSet(mantissa, exponent, 0);
            BEAST_EXPECT(result2) && BEAST_EXPECT(*result2 == floatIntMax);
        }

        {
            // hfs.floatToMantissaAndExponent(makeSlice(floatIntMin));
            vrt.setBytes(0, floatIntMin.data(), floatIntMin.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(floatToMantissaAndExponent_wrap,
                   &import[55],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   256,
                   8,
                   512,
                   4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const mantissa = vrt.getInt64(params, 2);
            auto const exponent = vrt.getInt32(params, 4);
            BEAST_EXPECT(mantissa == (std::numeric_limits<int64_t>::min() / 10) - 1) &&
                BEAST_EXPECT(exponent == 1);

            // roundtrip
            auto const result2 = hfs.floatSet(mantissa, exponent, 0);
            BEAST_EXPECT(result2) && BEAST_EXPECT(*result2 == floatIntMin);
        }

        {
            // hfs.floatToMantissaAndExponent(makeSlice(floatMax));
            vrt.setBytes(0, floatMax.data(), floatMax.size());
            WasmValVec params(6), result(1);
            auto* trap =
                ww(floatToMantissaAndExponent_wrap,
                   &import[55],
                   params,
                   result,
                   0,
                   FLOAT_SIZE,
                   256,
                   8,
                   512,
                   4);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const mantissa = vrt.getInt64(params, 2);
            auto const exponent = vrt.getInt32(params, 4);
            BEAST_EXPECT(mantissa == Number::maxRep) &&
                BEAST_EXPECT(exponent == Number::maxExponent);

            // roundtrip
            auto const result2 = hfs.floatSet(mantissa, exponent, 0);
            BEAST_EXPECT(result2) && BEAST_EXPECT(*result2 == floatMax);
        }
    }

    void
    testFloatNegate()
    {
        testcase("floatNegate");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.floatNegate(makeSlice(invalid));
            vrt.setBytes(0, invalid.data(), invalid.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatNegate_wrap, &import[56], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatNegate(makeSlice(floatIntZero));
            vrt.setBytes(0, floatIntZero.data(), floatIntZero.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatNegate_wrap, &import[56], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatIntZero);
        }

        {
            // hfs.floatNegate(makeSlice(float1));
            vrt.setBytes(0, float1.data(), float1.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatNegate_wrap, &import[56], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatMinus1);
        }

        {
            // hfs.floatNegate(makeSlice(floatMinus1));
            vrt.setBytes(0, floatMinus1.data(), floatMinus1.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatNegate_wrap, &import[56], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == float1);
        }

        {
            // hfs.floatNegate(makeSlice(floatIntMax));
            auto const expected = hfs.floatFromInt(std::numeric_limits<int64_t>::min() + 1, 0);
            vrt.setBytes(0, floatIntMax.data(), floatIntMax.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatNegate_wrap, &import[56], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(expected) && BEAST_EXPECT(resultBytes == *expected);
        }

        {
            // hfs.floatNegate(makeSlice(floatMaxExp));
            vrt.setBytes(0, floatMaxExp.data(), floatMaxExp.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatNegate_wrap, &import[56], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatMinusMaxExp);
        }

        {
            // hfs.floatNegate(makeSlice(floatPi));
            vrt.setBytes(0, floatPi.data(), floatPi.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatNegate_wrap, &import[56], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);

            // hfs.floatNegate(makeSlice(*result));
            vrt.setBytes(512, resultBytes.data(), resultBytes.size());
            WasmValVec params2(4), result2(1);
            auto* trap2 = ww(
                floatNegate_wrap, &import[56], params2, result2, 512, FLOAT_SIZE, 768, FLOAT_SIZE);

            BEAST_EXPECT(!trap2) && BEAST_EXPECT(result2[0].kind == WASM_I32) &&
                BEAST_EXPECT(result2[0].of.i32 == FLOAT_SIZE);
            auto const negPiBytes = vrt.getBytes(params2, 2);
            BEAST_EXPECT(negPiBytes == floatPi);
        }
    }

    void
    testFloatAbs()
    {
        testcase("floatAbs");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        {
            // hfs.floatAbs(makeSlice(invalid));
            vrt.setBytes(0, invalid.data(), invalid.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatAbs_wrap, &import[57], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(
                    result[0].of.i32 ==
                    static_cast<int32_t>(HostFunctionError::FLOAT_INPUT_MALFORMED));
        }

        {
            // hfs.floatAbs(makeSlice(floatIntZero));
            vrt.setBytes(0, floatIntZero.data(), floatIntZero.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatAbs_wrap, &import[57], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatIntZero);
        }

        {
            // hfs.floatAbs(makeSlice(float1));
            vrt.setBytes(0, float1.data(), float1.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatAbs_wrap, &import[57], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == float1);
        }

        {
            // hfs.floatAbs(makeSlice(floatMinus1));
            vrt.setBytes(0, floatMinus1.data(), floatMinus1.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatAbs_wrap, &import[57], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == float1);
        }

        {
            // hfs.floatAbs(makeSlice(floatIntMax));
            vrt.setBytes(0, floatIntMax.data(), floatIntMax.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatAbs_wrap, &import[57], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatIntMax);
        }

        {
            // hfs.floatAbs(makeSlice(floatIntMin));
            auto const negated = hfs.floatNegate(makeSlice(floatIntMin));
            vrt.setBytes(0, floatIntMin.data(), floatIntMin.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatAbs_wrap, &import[57], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(negated) && BEAST_EXPECT(resultBytes == *negated);
        }

        {
            // hfs.floatAbs(makeSlice(floatMinusMaxExp));
            vrt.setBytes(0, floatMinusMaxExp.data(), floatMinusMaxExp.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatAbs_wrap, &import[57], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(resultBytes == floatMaxExp);
        }

        {
            // hfs.floatAbs(makeSlice(floatMinus3));
            auto const expected = hfs.floatFromInt(3, 0);
            vrt.setBytes(0, floatMinus3.data(), floatMinus3.size());
            WasmValVec params(4), result(1);
            auto* trap =
                ww(floatAbs_wrap, &import[57], params, result, 0, FLOAT_SIZE, 256, FLOAT_SIZE);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == FLOAT_SIZE);
            auto const resultBytes = vrt.getBytes(params, 2);
            BEAST_EXPECT(expected) && BEAST_EXPECT(resultBytes == *expected);
        }
    }

    void
    testFloats()
    {
        // for checking binary formats manually
        // printNumbersBin();

        testTraceFloat();
        testFloatFromInt();
        testFloatFromUint();
        testFloatFromSTAmount();
        testFloatFromSTNumber();
        testFloatToInt();
        testFloatToMantissaAndExponent();
        testFloatNegate();
        testFloatAbs();
        testFloatSet();
        testFloatCompare();
        testFloatAdd();
        testFloatSubtract();
        testFloatMultiply();
        testFloatDivide();
        testFloatRoot();
        testFloatPower();
        testFloatLog();
        testFloatSpecialCases();
    }

    void
    testVectorIndexes()
    {
        testcase("WasmValVec indicies");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        ApplyContext ac = createApplyContext(env, ov);
        auto const dummyEscrow = keylet::escrow(env.master, env.seq(env.master));
        VirtualRuntime vrt;
        WasmHostFunctionsImpl hfs(ac, dummyEscrow);

        auto import = xrpl::createWasmImport(hfs);
        hfs.setRT(&vrt);

        bool ex = false;
        try
        {
            // hfs.getLedgerSqn();
            WasmValVec params(2), result(1);
            // 3 parameters instead of 2
            auto* trap =
                ww(getLedgerSqn_wrap, &import[0], params, result, 0, sizeof(std::uint32_t), 1);

            BEAST_EXPECT(!trap) && BEAST_EXPECT(result[0].kind == WASM_I32) &&
                BEAST_EXPECT(result[0].of.i32 == sizeof(std::uint32_t)) &&
                BEAST_EXPECT(vrt.getUint32(params, 0) == env.current()->header().seq);
        }
        catch (std::exception const& e)
        {
            BEAST_EXPECTS(e.what() == std::string("Out of bound"), e.what());
            ex = true;
        }

        // const version
        ex = false;
        try
        {
            WasmValVec params(2);
            [[maybe_unused]] auto const x = params[2];
        }
        catch (std::exception const& e)
        {
            BEAST_EXPECTS(e.what() == std::string("Out of bound"), e.what());
            ex = true;
        }

        BEAST_EXPECT(ex);
    }

    void
    run() override
    {
        testGetLedgerSqn();
        testGetParentLedgerTime();
        testGetParentLedgerHash();
        testGetBaseFee();
        testIsAmendmentEnabled();
        testCacheLedgerObj();
        testGetTxField();
        testGetCurrentLedgerObjField();
        testGetLedgerObjField();
        testGetTxNestedField();
        testGetCurrentLedgerObjNestedField();
        testGetLedgerObjNestedField();
        testGetTxArrayLen();
        testGetCurrentLedgerObjArrayLen();
        testGetLedgerObjArrayLen();
        testGetTxNestedArrayLen();
        testGetCurrentLedgerObjNestedArrayLen();
        testGetLedgerObjNestedArrayLen();
        testUpdateData();
        testCheckSignature();
        testComputeSha512HalfHash();
        testKeyletFunctions();
        testGetNFT();
        testGetNFTIssuer();
        testGetNFTTaxon();
        testGetNFTFlags();
        testGetNFTTransferFee();
        testGetNFTSerial();
        testTrace();
        testTraceNum();
        testTraceAccount();
        testTraceAmount();
        testFloats();

        testVectorIndexes();
    }
};

BEAST_DEFINE_TESTSUITE(HostFuncImpl, app, xrpl);

}  // namespace test
}  // namespace xrpl
