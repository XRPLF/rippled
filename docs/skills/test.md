# Testing

JTx framework for in-memory ledger testing. Tests live in `src/test/`, derive from `beast::unit_test::suite`, and register with `BEAST_DEFINE_TESTSUITE`.

## Key Patterns

### Test File Structure
```cpp
class MyFeature_test : public beast::unit_test::suite {
    void testBasic() {
        testcase("basic");
        using namespace jtx;
        Env env{*this};
        // ... test logic ...
    }
    void run() override { testBasic(); }
};
BEAST_DEFINE_TESTSUITE(MyFeature, app, ripple);
```

### Amendment Gating
```cpp
// REQUIRED: test with AND without the feature amendment
Env env{*this};                                           // all amendments on
Env env{*this, testable_amendments() - featureMyFeature}; // feature disabled
// With custom config:
Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
    /* modify config */ return cfg;
})};
```

### Transaction Submission
```cpp
auto const alice = Account{"alice"};
auto const bob = Account{"bob"};
env.fund(XRP(10000), alice, bob);
env.close();  // REQUIRED between setup and test transactions

env(pay(alice, bob, XRP(100)));                        // expects tesSUCCESS
env(pay(alice, bob, XRP(999999)), ter(tecUNFUNDED_PAYMENT));  // specific TER
env(pay(alice, bob, XRP(100)), txflags(tfPartialPayment));    // with flags
```

### State Verification
```cpp
env.require(balance(alice, XRP(9900)));
env.require(balance(alice, USD(1000)));
env.require(owners(alice, 2));

auto const sle = env.le(keylet::account(alice.id()));
BEAST_EXPECT(sle);
BEAST_EXPECT((*sle)[sfBalance] == XRP(9900));
```

## Review Checklist

- New transaction types MUST have tests with AND without the feature amendment
- Every error path (tem*, tec*, tef*) must have a test exercising it
- `env.close()` required between transactions that need ledger boundaries
- Verify state after transaction, not just TER code
- Test class naming: `FeatureName_test`, registered with `BEAST_DEFINE_TESTSUITE`

## Common Pitfalls

- Forgetting `*this` as first Env arg disconnects assertion reporting
- Comparing XRPAmount with raw integers instead of `XRP()` or `drops()`
- Trust lines must be established before IOU payments
- Each Env creates a full Application — keep test methods focused

## Key Files

- `src/test/jtx/Env.h` — test environment class
- `src/test/jtx/jtx.h` — convenience header (includes all helpers)
- `src/test/jtx/Account.h` — test accounts
- `src/test/jtx/amount.h` — XRP(), drops(), IOU helpers
