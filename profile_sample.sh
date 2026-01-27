#!/usr/bin/env bash
set -euo pipefail

SCRIPT_PATH="${1:-examples/computer_graphics/indexed_colors_examples.js}"
DURATION="${2:-8}"
INTERVAL="${3:-1}"
OUT_FILE="${4:-/private/tmp/ps_sample.txt}"
LOG_FILE="${5:-/private/tmp/ps_sample_run.log}"

if [[ ! -x "./protoscript" ]]; then
  echo "Missing ./protoscript in repo root" >&2
  exit 1
fi

if [[ ! -f "$SCRIPT_PATH" ]]; then
  echo "Missing script: $SCRIPT_PATH" >&2
  exit 1
fi

SAMPLER=""
if [[ -x "/usr/local/bin/simple" ]]; then
  SAMPLER="/usr/local/bin/simple"
elif [[ -x "/usr/bin/sample" ]]; then
  SAMPLER="/usr/bin/sample"
else
  echo "Missing sampler: /usr/local/bin/simple or /usr/bin/sample" >&2
  exit 1
fi

SUDO_CMD=()
if [[ "${SAMPLE_SUDO:-0}" == "1" ]]; then
  if command -v sudo >/dev/null 2>&1; then
    SUDO_CMD=(sudo)
  fi
fi

echo "Running: ./protoscript $SCRIPT_PATH" >&2
./protoscript "$SCRIPT_PATH" >"$LOG_FILE" 2>&1 &
PID=$!

echo "Sampling pid=$PID for ${DURATION}s (interval ${INTERVAL}ms) via $SAMPLER" >&2
set +e
if [[ "$SAMPLER" == "/usr/bin/sample" ]]; then
  "${SUDO_CMD[@]}" "$SAMPLER" "$PID" "$DURATION" "$INTERVAL" -mayDie -file "$OUT_FILE"
else
  "$SAMPLER" "$PID" "$DURATION" "$INTERVAL" -mayDie -file "$OUT_FILE"
fi
SAMPLE_STATUS=$?
set -e

wait "$PID" || true

if [[ $SAMPLE_STATUS -ne 0 ]]; then
  echo "Sampler exited with status $SAMPLE_STATUS" >&2
  exit $SAMPLE_STATUS
fi

if [[ -f "$OUT_FILE" ]]; then
  echo "Sample report: $OUT_FILE" >&2
else
  echo "Sample report not found: $OUT_FILE" >&2
  exit 1
fi
