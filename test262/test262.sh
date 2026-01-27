#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -x "$SCRIPT_DIR/protoscript" ]]; then
  ROOT="$SCRIPT_DIR"
elif [[ -x "$SCRIPT_DIR/../protoscript" ]]; then
  ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
else
  echo "Missing executable: protoscript (expected in repo root)" >&2
  exit 1
fi

if [[ -f "$SCRIPT_DIR/protoscript.txt" ]]; then
  LIST_FILE="$SCRIPT_DIR/protoscript.txt"
else
  LIST_FILE="$ROOT/test262/protoscript.txt"
fi

ASSERT_FILE="$ROOT/test262/assert.js"
PS_BIN="$ROOT/protoscript"

if [[ ! -x "$PS_BIN" ]]; then
  echo "Missing executable: $PS_BIN" >&2
  exit 1
fi

if [[ ! -f "$LIST_FILE" ]]; then
  echo "Missing list file: $LIST_FILE" >&2
  exit 1
fi

if [[ ! -f "$ASSERT_FILE" ]]; then
  echo "Missing assert harness: $ASSERT_FILE" >&2
  exit 1
fi

pass=0
fail=0
missing=0

while IFS= read -r test_path; do
  [[ -z "$test_path" ]] && continue
  [[ "$test_path" = \#* ]] && continue

  full_path="$ROOT/$test_path"
  if [[ ! -f "$full_path" ]]; then
    echo "Missing test: $test_path" >&2
    missing=$((missing + 1))
    continue
  fi

  tmp_template="${TMPDIR:-/tmp}/ps_test262_XXXXXX"
  tmp_file="$(mktemp "$tmp_template")"
  printf 'ProtoScript.include("%s");\n' "$ASSERT_FILE" > "$tmp_file"
  cat "$full_path" >> "$tmp_file"

  neg_parse=0
  if grep -q 'negative:' "$full_path"; then
    if grep -q 'phase:[[:space:]]*parse' "$full_path"; then
      neg_parse=1
    fi
  fi

  out="$("$PS_BIN" "$tmp_file" 2>&1)"
  status=$?
  rm -f "$tmp_file"
  if [[ $neg_parse -eq 1 ]]; then
    if [[ $status -ne 0 ]] && printf '%s\n' "$out" | grep -Eq 'Parse error|SyntaxError'; then
      pass=$((pass + 1))
    else
      echo "$out" >&2
      echo "FAIL: $test_path (negative parse) (exit $status)" >&2
      fail=$((fail + 1))
    fi
  else
    if [[ $status -eq 0 ]]; then
      pass=$((pass + 1))
    else
      echo "$out" >&2
      echo "FAIL: $test_path (exit $status)" >&2
      fail=$((fail + 1))
    fi
  fi
done < "$LIST_FILE"

printf "Done. pass=%d fail=%d missing=%d\n" "$pass" "$fail" "$missing"

if [[ $fail -ne 0 || $missing -ne 0 ]]; then
  exit 1
fi
