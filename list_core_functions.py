#!/usr/bin/env python3
"""
Extract core transaction handler functions for testing.
Focuses on the critical path functions in lending protocol.
"""
import json
import re
from pathlib import Path

def extract_core_functions():
    """Extract and categorize core functions by transaction type."""
    
    # Transaction handlers
    tx_handlers = [
        'LoanSet',
        'LoanDelete', 
        'LoanManage',
        'LoanPay',
        'LoanBrokerSet',
        'LoanBrokerDelete',
        'LoanBrokerCoverDeposit',
        'LoanBrokerCoverWithdraw',
        'LoanBrokerCoverClawback'
    ]
    
    # Critical functions for each handler
    critical_funcs = ['preflight', 'preclaim', 'doApply', 'checkSign', 'calculateBaseFee']
    
    results = {}
    
    for handler in tx_handlers:
        results[handler] = {
            'file': f'src/xrpld/app/tx/detail/{handler}.cpp',
            'functions': []
        }
        
        file_path = Path(f'src/xrpld/app/tx/detail/{handler}.cpp')
        if not file_path.exists():
            continue
            
        with open(file_path, 'r') as f:
            content = f.read()
            lines = content.split('\n')
            
        for func in critical_funcs:
            # Find function definition line
            pattern = re.compile(rf'{handler}::{func}\s*\(')
            for i, line in enumerate(lines, 1):
                if pattern.search(line):
                    results[handler]['functions'].append({
                        'name': func,
                        'line': i,
                        'is_critical': True
                    })
                    break
    
    return results

def extract_helper_functions():
    """Extract critical helper functions from LendingHelpers."""
    helper_file = Path('src/xrpld/app/misc/detail/LendingHelpers.cpp')
    
    if not helper_file.exists():
        return []
    
    with open(helper_file, 'r') as f:
        content = f.read()
    
    # Find key helper functions
    critical_helpers = [
        'loanPeriodicRate',
        'calculateFullPaymentInterest',
        'calculateRawLoanState',
        'calculateRoundedLoanState',
        'loanMakeFullPayment',
        'loanMakePayment',
        'computePaymentComponents',
        'computeFee',
        'computeLoanProperties',
        'roundPeriodicPayment',
        'valueMinusFee'
    ]
    
    found = []
    lines = content.split('\n')
    
    for func in critical_helpers:
        pattern = re.compile(rf'\b{func}\s*\(')
        for i, line in enumerate(lines, 1):
            if pattern.search(line) and not line.strip().startswith('//'):
                found.append({
                    'name': func,
                    'file': 'src/xrpld/app/misc/detail/LendingHelpers.cpp',
                    'line': i
                })
                break
    
    return found

def main():
    print("=" * 80)
    print("CORE FUNCTIONS TO TEST - Lending Protocol XLS-66")
    print("=" * 80)
    print()
    
    # Transaction handlers
    print("TRANSACTION HANDLERS")
    print("-" * 80)
    handlers = extract_core_functions()
    
    for handler, data in handlers.items():
        if not data['functions']:
            continue
        print(f"\n{handler} ({data['file']})")
        for func in data['functions']:
            print(f"  ✓ {func['name']:20s} @ line {func['line']}")
    
    print("\n" + "=" * 80)
    print("HELPER FUNCTIONS (Arithmetic & State Management)")
    print("-" * 80)
    
    helpers = extract_helper_functions()
    for helper in helpers:
        print(f"  ✓ {helper['name']:30s} @ {helper['file']}:{helper['line']}")
    
    print("\n" + "=" * 80)
    print("TESTING PRIORITY")
    print("-" * 80)
    print("""
HIGH PRIORITY (Arithmetic & Authorization):
  1. LendingHelpers::loanPeriodicRate - Interest rate calculations
  2. LendingHelpers::computePaymentComponents - Payment split logic
  3. LoanPay::doApply - Payment processing 
  4. LoanSet::checkSign - Signature verification
  5. LoanSet::doApply - Loan creation logic

MEDIUM PRIORITY (State Management):
  6. LoanManage::doApply - Loan modification
  7. LoanBrokerCoverWithdraw::preclaim - Collateral checks
  8. LendingHelpers::calculateRoundedLoanState - Rounding logic
  9. LoanDelete::doApply - Cleanup logic

LOW PRIORITY (Basic Validation):
  10. All preflight functions - Static validation
    """)

if __name__ == '__main__':
    main()
