#!/bin/sh
set -eu

make clean
make

run_case() {
    name=$1
    expect_rc=$2
    printf '%s: ' "$name"

    out=$(mktemp)
    rc=0
    if ./protoscript "tests/cases/$name.js" >"$out" 2>/dev/null; then
        rc=0
    else
        rc=$?
    fi

    if [ "$expect_rc" -eq 0 ]; then
        if [ "$rc" -ne 0 ]; then
            rm -f "$out"
            echo "FAIL"
            exit 1
        fi
    else
        if [ "$rc" -eq 0 ]; then
            rm -f "$out"
            echo "FAIL"
            exit 1
        fi
    fi

    if ! cmp -s "$out" "tests/cases/$name.out"; then
        rm -f "$out"
        echo "FAIL"
        exit 1
    fi

    rm -f "$out"
    echo "PASS"
}

run_case 01-empty 0
run_case 02-whitespace 0
run_case 03-print-string 0
run_case 04-print-number 0
run_case 05-print-var 0
run_case 06-var-reassign 0
run_case 07-add 0
run_case 08-string 0
run_case 09-syntax-error 1
run_case 118-unterminated-comment 1
run_case 10-function-add 0
run_case 11-while 0
run_case 12-for 0
run_case 13-comments 0
run_case 14-compare 0
run_case 15-equality 0
run_case 16-logical 0
run_case 17-bitwise 0
run_case 18-conditional-comma 0
run_case 19-do-while 0
run_case 20-switch 0
run_case 21-break-continue 0
run_case 22-for-in 0
with_enabled=$(awk '/^#define PS_ENABLE_WITH/ {print $3}' include/ps_config.h)
eval_enabled=$(awk '/^#define PS_ENABLE_EVAL/ {print $3}' include/ps_config.h)
alias_enabled=$(awk '/^#define PS_ENABLE_ARGUMENTS_ALIASING/ {print $3}' include/ps_config.h)
if [ "${with_enabled:-0}" -eq 1 ]; then
    run_case 23-with 0
fi
run_case 24-try-catch-finally 0
run_case 25-hoist-var 0
run_case 26-hoist-fn 0
run_case 27-label-break 0
run_case 28-label-continue 0
if [ "${with_enabled:-0}" -eq 1 ]; then
    run_case 29-with-primitive 0
fi
run_case 30-delete-primitive 0
run_case 31-typeof-undeclared 0
if [ "${eval_enabled:-0}" -eq 0 ]; then
    run_case 32-eval-disabled 1
else
    run_case 94-eval-basic 0
    run_case 114-eval-edge 0
fi
if [ "${alias_enabled:-0}" -eq 1 ]; then
    run_case 33-arguments-aliasing 0
    run_case 92-arguments-aliasing-full 0
fi
run_case 34-computed-member 0
run_case 35-reference-error 1
if [ "${with_enabled:-0}" -eq 1 ]; then
    run_case 36-type-error 1
    run_case 113-with-edge 0
fi
run_case 37-native-objects 0
run_case 38-new 0
run_case 39-prototype-methods 0
run_case 40-math-date-regexp 0
run_case 41-more-builtins 0
run_case 42-builtins-rounding 0
run_case 43-string-edge 0
run_case 44-array-like-concat 0
run_case 45-typeerror-receivers 0
run_case 46-string-edge2 0
run_case 47-array-like-sparse 0
run_case 48-call-apply 0
run_case 49-call-apply-edge 0
run_case 50-bind 0
run_case 51-bind-new-return 0
run_case 52-apply-length-overflow 0
run_case 53-bind-new-primitive 0
run_case 54-bind-new-null-undefined 0
run_case 55-bind-new-function-return 0
run_case 56-bind-new-function-props 0
run_case 57-bind-new-function-proto 0
run_case 58-bind-new-function-proto-inherited 0
run_case 59-date-parse-utc 0
run_case 60-date-utc-overflow 0
run_case 61-date-parse-iso 0
run_case 62-date-parse-offsets 0
run_case 63-math-random 0
run_case 64-regexp-exec-test 0
run_case 65-math-complete 0
run_case 66-date-ctor 0
run_case 67-regexp-engine 0
run_case 68-string-escapes 0
run_case 69-error-objects 0
run_case 70-tonumber 0
run_case 71-equality-es1 0
run_case 72-regexp-lastindex 0
run_case 73-date-call 0
run_case 74-this-boxing 0
run_case 75-object-prop-enum 0
run_case 76-object-enum 0
run_case 77-builtin-dontenum 0
run_case 78-math-readonly 0
run_case 79-builtin-readonly 0
run_case 80-prototype-attrs 0
run_case 81-global-attrs 0
run_case 82-function-length-name 0
run_case 83-array-reverse-sort-splice 0
run_case 84-string-more 0
run_case 85-regexp-literal 0
run_case 86-number-precision-exponential 0
run_case 87-tonumber-grammar 0
run_case 88-date-local 0
run_case 89-regexp-ignorecase-unicode 0
run_case 90-regexp-robustness 0
run_case 91-identifier-unicode 0
run_case 93-activation-object 0
run_case 95-activation-attrs 0
run_case 96-function-constructor 0
run_case 97-string-fromcharcode 0
run_case 98-number-radix 0
run_case 99-array-ctor-length 0
run_case 100-native-edge-errors 0
run_case 101-native-radix-edge 0
run_case 102-string-fromcharcode-edge 0
run_case 103-native-more-edge 0
run_case 104-string-boundaries 0
run_case 105-array-length-write 0
run_case 106-number-radix-fractional 0
run_case 107-operators-control-flow 0
run_case 108-error-semantics 0
run_case 109-switch-label-edge 0
run_case 110-operator-precedence 0
run_case 111-runtime-errors 0
run_case 112-operator-edge 0
run_case 115-builtins-edge 0
run_case 116-string-length 0
run_case 117-glyph-length 0
run_case 119-function-details 0
run_case 120-regexp-ignorecase-extended 0
run_case 121-single-quote 0
run_case 122-string-search 0
run_case 123-regexp-static-captures 0
