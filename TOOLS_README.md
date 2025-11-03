# Audit Tools for XLS-66 Lending Protocol

This document describes the audit and testing tools available for the XRPL Immunefi Attackathon.

## Available Tools

### 1. **audit_scope_analyzer.py** 🔍
**Purpose**: Automatically extract and categorize all functions from the lending protocol

**Usage**:
```bash
python3 audit_scope_analyzer.py
```

**Output**:
- `AUDIT_SCOPE.md` - Human-readable function inventory (806 lines)
- `audit_scope.json` - Machine-readable data for automation

**What it finds**:
- All functions in Loan*.cpp/h files
- Categorizes by: validation, helpers, core_execution, arithmetic, etc.
- Marks critical functions with 🔴
- Provides audit checklists for each function

**Example output**:
```
Category Summary:
  helpers       :   1 functions (1 critical)
  other         :   3 functions (0 critical)
  test          :  36 functions (3 critical)
  validation    :   9 functions (9 critical)
```

---

### 2. **list_core_functions.py** 🎯
**Purpose**: Extract and prioritize core transaction handler functions

**Usage**:
```bash
python3 list_core_functions.py
```

**Output**: Prioritized list of all critical functions with line numbers

**Shows**:
- **Transaction Handlers**: All 9 loan transaction types (LoanSet, LoanPay, etc.)
- **Helper Functions**: 11 critical arithmetic/state functions from LendingHelpers.cpp
- **Priority Rankings**: HIGH/MEDIUM/LOW based on security impact

**Key Functions Identified**:
```
HIGH PRIORITY:
  ✓ loanPeriodicRate (line 58) - Interest calculations
  ✓ computePaymentComponents (line 1003) - Payment split logic
  ✓ LoanPay::doApply (line 232) - Payment processing
  ✓ LoanSet::checkSign (line 126) - Signature verification
  ✓ LoanSet::doApply (line 322) - Loan creation
```

---

### 3. **show_function.sh** 👁️
**Purpose**: Quick function viewer with context

**Usage**:
```bash
./show_function.sh <function_name>

# Examples:
./show_function.sh loanPeriodicRate
./show_function.sh doApply
./show_function.sh checkSign
```

**Output**: Shows function signature + 10 lines of context for quick review

---

### 4. **AUDIT_CHECKLIST.md** 📋
**Purpose**: Systematic security review checklist

**Contents**:
- ✓ 9 Transaction handlers with attack vectors
- ✓ Helper function security considerations
- ✓ Invariants to verify
- ✓ Amendment dependency checks
- ✓ 5 Economic attack categories
- ✓ Integration point risks
- ✓ Code quality checks
- ✓ Test coverage analysis

**Attack Vectors Covered**:
1. Input validation bypasses
2. Signature forgery
3. Integer overflow/underflow
4. Fee calculation exploits
5. Collateral requirement bypasses
6. Flash loan attacks
7. Liquidation manipulation
8. Interest rate exploits
9. Griefing attacks
10. Front-running

---

### 5. **audit_scope.json** 📊
**Purpose**: Machine-readable function inventory for scripting

**Usage**:
```bash
# Extract all critical functions
python3 -c "
import json
data = json.load(open('audit_scope.json'))
for cat in data['functions'].values():
    for f in cat:
        if f['is_critical']:
            print(f'{f[\"name\"]} @ {f[\"file\"]}:{f[\"line\"]}')
"
```

**Data Structure**:
```json
{
  "total_functions": 49,
  "categories": {
    "validation": 9,
    "helpers": 1,
    "test": 36
  },
  "functions": {
    "validation": [
      {
        "name": "checkLendingProtocolDependencies",
        "file": "src/xrpld/app/tx/detail/LoanSet.cpp",
        "line": 31,
        "is_critical": true,
        "signature": "..."
      }
    ]
  }
}
```

---

## Quick Start Workflow

### Step 1: Identify Functions to Test
```bash
# Get complete function inventory
python3 list_core_functions.py > core_functions.txt

# Or get ALL functions
python3 audit_scope_analyzer.py
```

### Step 2: Review Specific Function
```bash
# Quick view with context
./show_function.sh loanPeriodicRate

# Or view full file
code src/xrpld/app/misc/detail/LendingHelpers.cpp:58
```

### Step 3: Run Tests
```bash
cd .build

# Run all lending tests
./rippled --unittest Loan
./rippled --unittest LoanBroker

# Run specific test
./rippled --unittest "ripple.tx.Loan.Lifecycle"
```

### Step 4: Check Against Audit Checklist
```bash
# Open checklist and mark items as you review
vim AUDIT_CHECKLIST.md
```

---

## Testing Helpers

### Run Specific Test Pattern
```bash
cd .build
./rippled --unittest "ripple.tx.Loan.*Interest*"
```

### Get Function Signatures
```bash
# From JSON
jq '.functions.validation[] | select(.is_critical) | {name, signature, line}' audit_scope.json

# From source
grep -A 5 "doApply" src/xrpld/app/tx/detail/LoanPay.cpp | head -10
```

### Find All Uses of a Function
```bash
# Find where loanPeriodicRate is called
grep -rn "loanPeriodicRate" src/xrpld/app/tx/detail/ src/xrpld/app/misc/
```

---

## Priority Testing Order

Based on attack surface analysis:

### Phase 1: Arithmetic Functions (CRITICAL)
```
src/xrpld/app/misc/detail/LendingHelpers.cpp:
  → loanPeriodicRate (line 58)
  → computePaymentComponents (line 1003)
  → calculateRoundedLoanState (line 606)
  → roundPeriodicPayment (line 1029)
```

**Why**: Integer overflow, precision loss, rounding exploits

### Phase 2: Payment Processing (HIGH)
```
src/xrpld/app/tx/detail/LoanPay.cpp:
  → doApply (line 232)
  → preclaim (line 130)
  → calculateBaseFee (line 60)
```

**Why**: Principal/interest split, fee bypass, overpayment handling

### Phase 3: Loan Creation (HIGH)
```
src/xrpld/app/tx/detail/LoanSet.cpp:
  → checkSign (line 126)
  → doApply (line 322)
  → preclaim (line 204)
```

**Why**: Signature verification, collateral requirements, authorization

### Phase 4: Broker Operations (MEDIUM)
```
src/xrpld/app/tx/detail/LoanBrokerCoverWithdraw.cpp:
  → preclaim (line 58)
  → doApply (line 144)
```

**Why**: Collateralization ratio, withdrawal limits

### Phase 5: State Management (MEDIUM)
```
src/xrpld/app/tx/detail/LoanManage.cpp:
  → doApply (line 383)

src/xrpld/app/tx/detail/LoanDelete.cpp:
  → doApply (line 80)
```

**Why**: State transitions, cleanup logic

---

## Constants to Review

From `src/xrpld/app/tx/detail/LoanSet.h`:

```cpp
constexpr static std::uint32_t minPaymentTotal = 1;      // Line ~346
constexpr static std::uint32_t minPaymentInterval = 60;  // Line ~347
constexpr static std::uint32_t defaultGracePeriod = 60;  // Line ~348
```

**Questions**:
- Is minPaymentTotal = 1 too low? (dust attacks)
- Is 60 second interval exploitable?
- Is 60 second grace period too short?

---

## Extracting More Information

### Find All Transaction Types
```bash
ls src/xrpld/app/tx/detail/Loan*.cpp | xargs basename -a
```

### Count Lines of Code
```bash
find src/xrpld/app/tx/detail/Loan*.cpp -type f -exec wc -l {} + | tail -1
find src/xrpld/app/misc/detail/LendingHelpers.cpp -type f -exec wc -l {} +
```

### Get Test Coverage
```bash
# Count test assertions for Loan tests
grep -c "BEAST_EXPECT\|require\|pass\|fail" src/test/app/Loan_test.cpp
```

---

## Tips for Bug Hunting

1. **Follow the Money**: Focus on functions that move assets or calculate amounts
2. **Check Boundaries**: Look for edge cases in min/max values
3. **Test Rounding**: Arithmetic functions - can rounding be exploited?
4. **Verify Authorization**: Can operations be performed by wrong parties?
5. **State Consistency**: Are all state updates atomic?
6. **Fee Calculation**: Can fees be minimized or bypassed?
7. **Time Manipulation**: Can timestamp-based logic be gamed?

---

## Report Template

When you find an issue, use this format:

```markdown
### [SEVERITY] Issue Title

**Location**: `file.cpp:line_number`

**Function**: `functionName()`

**Description**:
Clear explanation of the vulnerability

**Impact**:
What can attacker do? Worst case scenario?

**Proof of Concept**:
1. Step one
2. Step two
3. Result

**Recommendation**:
How to fix

**References**:
- Related code
- Similar issues
```

---

## Additional Resources

- **Competition**: https://immunefi.com/audit-competition/xrpl-ripple-attackathon/information/
- **XLS-66 Spec**: https://github.com/XRPLF/XRPL-Standards/tree/master/XLS-0066d-lending
- **Test Files**: `src/test/app/Loan_test.cpp`, `src/test/app/LoanBroker_test.cpp`
- **Build**: `.build/rippled` (1.6GB with debug symbols)

---

**Last Updated**: 2025-11-03
**Status**: Ready for attackathon
**Test Results**: All 14,389 lending tests passing ✓
