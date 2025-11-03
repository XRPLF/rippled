#!/bin/bash
# Quick function viewer for lending protocol
# Usage: ./show_function.sh <function_name>

FUNC=$1

if [ -z "$FUNC" ]; then
    echo "Usage: ./show_function.sh <function_name>"
    echo ""
    echo "Examples:"
    echo "  ./show_function.sh loanPeriodicRate"
    echo "  ./show_function.sh doApply"
    echo "  ./show_function.sh checkSign"
    exit 1
fi

echo "Searching for function: $FUNC"
echo "========================================"

# Search in lending protocol files
grep -n "^[^/]*\b${FUNC}\s*(" \
    src/xrpld/app/tx/detail/Loan*.cpp \
    src/xrpld/app/tx/detail/Loan*.h \
    src/xrpld/app/misc/detail/LendingHelpers.cpp \
    src/xrpld/app/misc/LendingHelpers.h \
    2>/dev/null | while IFS=: read file line content; do
    
    echo ""
    echo "File: $file"
    echo "Line: $line"
    echo "----------------------------------------"
    
    # Show context (10 lines)
    sed -n "$((line-2)),$((line+10))p" "$file" | head -13
    echo "----------------------------------------"
done

