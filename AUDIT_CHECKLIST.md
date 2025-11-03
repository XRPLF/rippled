# XRPL Lending Protocol (XLS-66) - Security Audit Checklist

**Competition**: Immunefi XRPL Ripple Attackathon
**Target**: PR #5270 - XLS-66 Lending Protocol Implementation
**Branch**: `ximinez/lending-XLS-66`

---

## 1. Transaction Handlers (Primary Attack Surface)

### 1.1 LoanSet (Create New Loans)
**Location**: `src/xrpld/app/tx/detail/LoanSet.{h,cpp}`

**Critical Functions**:
- [ ] `preflight()` - Static validation (no ledger access)
- [ ] `checkSign()` - Counter-party signature validation
- [ ] `calculateBaseFee()` - Fee calculation logic
- [ ] `preclaim()` - Validation with ledger access
- [ ] `doApply()` - State mutation logic

**Attack Vectors to Check**:
- [ ] **Input validation**: Can malformed parameters bypass checks?
- [ ] **Signature verification**: Can counterparty signatures be forged/bypassed?
- [ ] **Interest rate manipulation**: Can rates be set to extreme values?
- [ ] **Payment interval/total manipulation**: Integer overflow/underflow?
- [ ] **Collateral requirements**: Can loans be created without sufficient collateral?
- [ ] **Fee bypass**: Can base fee calculation be gamed?
- [ ] **Reserve requirements**: Are XRP reserves properly enforced?
- [ ] **Reentrancy**: Can state be modified during validation?

### 1.2 LoanDelete (Close Loans)
**Location**: `src/xrpld/app/tx/detail/LoanDelete.{h,cpp}`

**Critical Functions**:
- [ ] `preflight()`
- [ ] `preclaim()`
- [ ] `doApply()`

**Attack Vectors to Check**:
- [ ] **Authorization**: Can attacker delete others' loans?
- [ ] **Outstanding balance**: Can loans with debt be deleted?
- [ ] **State cleanup**: Are all references properly removed?
- [ ] **Fund recovery**: Are assets returned to correct parties?
- [ ] **Broker fee settlement**: Are outstanding fees settled correctly?

### 1.3 LoanManage (Modify Loans)
**Location**: `src/xrpld/app/tx/detail/LoanManage.{h,cpp}`

**Critical Functions**:
- [ ] `preflight()`
- [ ] `preclaim()`
- [ ] `doApply()`

**Attack Vectors to Check**:
- [ ] **Authorization**: Can only authorized parties modify loans?
- [ ] **Impairment flag**: Can it be manipulated to affect loan terms?
- [ ] **State transitions**: Are all state changes valid?
- [ ] **Payment schedule**: Can modifications break payment logic?

### 1.4 LoanPay (Make Payments)
**Location**: `src/xrpld/app/tx/detail/LoanPay.{h,cpp}`

**Critical Functions**:
- [ ] `preflight()`
- [ ] `preclaim()`
- [ ] `doApply()`

**Attack Vectors to Check**:
- [ ] **Payment amount**: Can overpayments cause issues?
- [ ] **Underpayments**: Are partial payments handled correctly?
- [ ] **Interest calculation**: Can interest be manipulated?
- [ ] **Principal/interest split**: Is allocation correct?
- [ ] **Fee distribution**: Are broker fees calculated/paid correctly?
- [ ] **Time manipulation**: Can payment timing be exploited?
- [ ] **Double payment**: Can same payment be counted twice?
- [ ] **Final payment**: Special case handling secure?

### 1.5 LoanBrokerSet (Create Brokers)
**Location**: `src/xrpld/app/tx/detail/LoanBrokerSet.{h,cpp}`

**Critical Functions**:
- [ ] `preflight()`
- [ ] `preclaim()`
- [ ] `doApply()`

**Attack Vectors to Check**:
- [ ] **Cover deposit requirements**: Can broker be created with insufficient collateral?
- [ ] **Rate parameters**: Can liquidation/min rates be set incorrectly?
- [ ] **Vault association**: Is vault ownership validated?
- [ ] **Authorization**: Who can create brokers?

### 1.6 LoanBrokerDelete
**Location**: `src/xrpld/app/tx/detail/LoanBrokerDelete.{h,cpp}`

**Critical Functions**:
- [ ] `preflight()`
- [ ] `preclaim()`
- [ ] `doApply()`

**Attack Vectors to Check**:
- [ ] **Outstanding loans**: Can broker be deleted with active loans?
- [ ] **Cover return**: Is collateral returned correctly?
- [ ] **Authorization**: Proper ownership checks?

### 1.7 LoanBrokerCoverDeposit
**Location**: `src/xrpld/app/tx/detail/LoanBrokerCoverDeposit.{h,cpp}`

**Critical Functions**:
- [ ] `preflight()`
- [ ] `preclaim()`
- [ ] `doApply()`

**Attack Vectors to Check**:
- [ ] **Amount validation**: Can zero or negative amounts be deposited?
- [ ] **Asset type**: Correct asset enforcement?
- [ ] **Balance updates**: Are balances updated atomically?
- [ ] **Maximum limits**: Can cover be increased without bound?

### 1.8 LoanBrokerCoverWithdraw
**Location**: `src/xrpld/app/tx/detail/LoanBrokerCoverWithdraw.{h,cpp}`

**Critical Functions**:
- [ ] `preflight()`
- [ ] `preclaim()`
- [ ] `doApply()`

**Attack Vectors to Check**:
- [ ] **Collateralization ratio**: Can withdrawal break collateral requirements?
- [ ] **Outstanding loan value**: Is total covered loan value checked?
- [ ] **Withdrawal limits**: Can broker withdraw more than available?
- [ ] **Liquidation triggering**: Does withdrawal trigger liquidation incorrectly?

### 1.9 LoanBrokerCoverClawback
**Location**: `src/xrpld/app/tx/detail/LoanBrokerCoverClawback.{h,cpp}`

**Critical Functions**:
- [ ] `preflight()`
- [ ] `preclaim()`
- [ ] `doApply()`

**Attack Vectors to Check**:
- [ ] **Authorization**: Can only vault owner clawback?
- [ ] **Asset validation**: Correct asset being clawed back?
- [ ] **Amount calculation**: Is clawback amount computed correctly?
- [ ] **Broker state**: Does clawback update broker state properly?

---

## 2. Helper Functions (Core Logic)

### 2.1 Interest Calculations
**Location**: `src/xrpld/app/misc/LendingHelpers.{h,cpp}`

**Critical Functions**:
- [ ] `loanPeriodicRate()` - Convert annual rate to periodic
- [ ] `calculateFullPaymentInterest()` - Interest for full payment
- [ ] `calculateRawLoanState()` - Compute loan state
- [ ] `calculateRoundedLoanState()` - Round loan state to asset precision

**Attack Vectors to Check**:
- [ ] **Integer arithmetic**: Overflow/underflow in calculations?
- [ ] **Rounding errors**: Can rounding be exploited for profit?
- [ ] **Precision loss**: Do conversions lose significant precision?
- [ ] **Rate limits**: Are interest rates bounded appropriately?
- [ ] **Time calculations**: Can time be manipulated (past payment dates)?

### 2.2 Payment Processing
**Location**: `src/xrpld/app/misc/LendingHelpers.{h,cpp}`

**Critical Functions**:
- [ ] `loanMakeFullPayment()` - Process full loan payment
- [ ] `loanMakePayment()` - Process partial payment
- [ ] `computePaymentComponents()` - Break down payment into parts
- [ ] `computeFee()` - Calculate management fee

**Attack Vectors to Check**:
- [ ] **Principal/interest split**: Incorrect allocation?
- [ ] **Fee calculation**: Can fees be minimized/bypassed?
- [ ] **Overpayment handling**: Is excess handled correctly?
- [ ] **Underpayment**: Are minimums enforced?
- [ ] **Payment order**: Can order of operations be exploited?
- [ ] **State consistency**: Are all state updates atomic?

### 2.3 Loan State Management
**Location**: `src/xrpld/app/misc/LendingHelpers.{h,cpp}`

**Critical Functions**:
- [ ] `computeLoanProperties()` - Initialize loan properties
- [ ] `roundPeriodicPayment()` - Ensure consistent rounding
- [ ] `valueMinusFee()` - Calculate value after fee deduction

**Attack Vectors to Check**:
- [ ] **State transitions**: Are all transitions valid?
- [ ] **Invariant preservation**: Do operations maintain loan invariants?
- [ ] **Rounding consistency**: Is rounding applied uniformly?

---

## 3. Invariants & Constraints

### 3.1 Loan Invariants
- [ ] `totalValueOutstanding >= principalOutstanding`
- [ ] `interestOutstanding = valueOutstanding - principalOutstanding`
- [ ] `interestDue + managementFeeDue = interestOutstanding`
- [ ] `paymentsRemaining > 0` while loan active
- [ ] `principalOutstanding >= 0`
- [ ] `periodicPayment > 0`

### 3.2 Broker Invariants
- [ ] `coverBalance >= sum(coveredLoans) * minCoverRate`
- [ ] `coverBalance <= maxCoverLimit` (if applicable)
- [ ] `liquidationRate >= minCoverRate`
- [ ] `debtMaximum >= sum(activeLoanPrincipals)`

### 3.3 Payment Invariants
- [ ] Payment must reduce `valueOutstanding`
- [ ] `principalPaid + interestPaid + feePaid = paymentAmount`
- [ ] Final payment must close loan (`paymentsRemaining = 0`)
- [ ] Overpayment must be handled or rejected

---

## 4. Amendment Dependencies

**Check**: `checkLendingProtocolDependencies()`

Required Amendments:
- [ ] `featureMPTokensV1` - Multi-Purpose Tokens
- [ ] `featureSingleAssetVault` - Vault functionality
- [ ] `featureLendingProtocol` - Lending itself

**Attack Vectors**:
- [ ] Can lending be used without required amendments?
- [ ] Are amendment checks bypassed in any code path?
- [ ] What happens if amendments are disabled mid-flight?

---

## 5. Economic Attack Vectors

### 5.1 Flash Loan Attacks
- [ ] Can attacker borrow and repay in same ledger?
- [ ] Are there atomic operations that can be exploited?
- [ ] Can price oracles be manipulated?

### 5.2 Liquidation Attacks
- [ ] Can attacker force premature liquidation?
- [ ] Can liquidation be prevented when it should occur?
- [ ] Is liquidation price manipulation possible?

### 5.3 Interest Rate Manipulation
- [ ] Can extremely high rates cause overflow?
- [ ] Can extremely low rates be profitable via rounding?
- [ ] Can rate changes mid-loan be exploited?

### 5.4 Griefing Attacks
- [ ] Can attacker lock up broker funds indefinitely?
- [ ] Can small loans DOS broker operations?
- [ ] Can dust amounts break accounting?

### 5.5 Front-Running
- [ ] Can attacker see pending loan and front-run?
- [ ] Are there MEV opportunities in loan creation/payments?

---

## 6. Integration Points

### 6.1 Vault Integration
- [ ] Is vault balance checked correctly?
- [ ] Can vault be drained via lending?
- [ ] Are vault permissions enforced?

### 6.2 MPToken Integration
- [ ] Are MPToken amounts validated?
- [ ] Can wrong token types be used?
- [ ] Are token transfers atomic?

### 6.3 Trust Line Integration
- [ ] Are trust lines created/destroyed correctly?
- [ ] Can trust line limits be bypassed?
- [ ] Are rippling rules enforced?

---

## 7. Code Quality Checks

### 7.1 Input Validation
- [ ] All user inputs validated
- [ ] Bounds checking on all numeric inputs
- [ ] Asset type validation
- [ ] Account existence checks

### 7.2 Error Handling
- [ ] All error paths return appropriate `TER` codes
- [ ] No silent failures
- [ ] Consistent error reporting
- [ ] Ledger left in valid state on error

### 7.3 State Consistency
- [ ] All state changes are atomic
- [ ] No partial updates on failure
- [ ] Rollback works correctly
- [ ] No orphaned ledger objects

### 7.4 Access Control
- [ ] Authorization checks on all mutations
- [ ] Signature requirements enforced
- [ ] Owner-only operations protected
- [ ] No privilege escalation

---

## 8. Test Coverage Analysis

**Location**: `src/test/app/Loan_test.cpp`, `src/test/app/LoanBroker_test.cpp`

### 8.1 Test Categories to Review
- [ ] Disabled amendment tests
- [ ] Basic loan lifecycle tests
- [ ] Broker cover tests
- [ ] Payment tests
- [ ] Edge case tests
- [ ] Failure condition tests

### 8.2 Missing Test Coverage
- [ ] Identify untested code paths
- [ ] Identify untested error conditions
- [ ] Identify untested edge cases
- [ ] Suggest additional tests

---

## 9. Specific Code Locations to Review

### High Priority Files:
1. `src/xrpld/app/misc/detail/LendingHelpers.cpp` - Core arithmetic
2. `src/xrpld/app/tx/detail/LoanPay.cpp` - Payment processing
3. `src/xrpld/app/tx/detail/LoanSet.cpp` - Loan creation
4. `src/xrpld/app/tx/detail/LoanBrokerCoverWithdraw.cpp` - Collateral management

### Constants to Verify:
**Location**: `src/xrpld/app/tx/detail/LoanSet.h`
- [ ] `minPaymentTotal = 1` - Is minimum too low?
- [ ] `minPaymentInterval = 60` - 60 seconds reasonable?
- [ ] `defaultGracePeriod = 60` - Grace period too short?

---

## 10. Report Template

For each finding, document:
```
### [SEVERITY] Finding Title

**Location**: `file.cpp:line_number`

**Description**:
Clear description of the vulnerability

**Impact**:
What can an attacker do? What is the worst case?

**Proof of Concept**:
Step-by-step exploit

**Recommendation**:
How to fix it

**References**:
Related code/issues
```

**Severity Levels**:
- **CRITICAL**: Direct loss of funds, protocol breakdown
- **HIGH**: Indirect loss of funds, major functionality break
- **MEDIUM**: Griefing, DOS, economic attacks
- **LOW**: Best practice violations, minor issues
- **INFO**: Code quality, optimization opportunities

---

## Progress Tracking

- [ ] Phase 1: Initial code review (all handlers)
- [ ] Phase 2: Helper function deep dive
- [ ] Phase 3: Invariant verification
- [ ] Phase 4: Economic analysis
- [ ] Phase 5: Integration testing
- [ ] Phase 6: Report writing

**Start Date**: 2025-11-03
**Auditors**: Team
**Status**: Ready to begin systematic review
