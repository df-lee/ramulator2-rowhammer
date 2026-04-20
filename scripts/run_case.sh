#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEMPLATE="${ROOT_DIR}/baseline_config.yaml"
GENERATED_DIR="${ROOT_DIR}/generated_configs"
LOG_DIR="${ROOT_DIR}/logs"

mkdir -p "${GENERATED_DIR}" "${LOG_DIR}"

: "${CASE_NAME:=default_case}"
: "${TRACE:=example_rh_physaddr.trace}"

: "${PROTECTION_FAMILY:=trr}"
: "${PROTECTION_SCOPE:=uniform}"

: "${NUM_ROWS:=32768}"
: "${COMPRESS_ROW_SPACE:=true}"
: "${HAMMER_THRESHOLD:=1}"

: "${TRR_THRESHOLD:=3}"
: "${TRR_THRESHOLD_CRITICAL:=2}"
: "${TRR_THRESHOLD_NONCRITICAL:=4}"

: "${PARA_P_UNIFORM:=0.01}"
: "${PARA_P_CRITICAL:=0.10}"
: "${PARA_P_NONCRITICAL:=0.01}"

: "${CRITICAL_ROW_STRIDE:=16}"
: "${CRITICAL_ROW_OFFSET:=0}"

: "${FLIP_MODE:=probabilistic}"
: "${FLIP_THRESHOLD:=4}"

: "${PROB_FUNCTION:=linear}"
: "${PROB_FLIP_START:=2}"
: "${PROB_ALPHA:=0.2}"
: "${PROB_K:=1.0}"
: "${PROB_MIDPOINT:=4.0}"
: "${RANDOM_SEED:=12345}"

: "${BUDGET_WINDOW:=1000}"
: "${BUDGET_PER_WINDOW:=32}"

: "${DEBUG:=false}"

CONFIG_FILE="${GENERATED_DIR}/${CASE_NAME}.yaml"
LOG_FILE="${LOG_DIR}/${CASE_NAME}.log"

export TRACE
export PROTECTION_FAMILY
export PROTECTION_SCOPE
export NUM_ROWS
export COMPRESS_ROW_SPACE
export HAMMER_THRESHOLD
export TRR_THRESHOLD
export TRR_THRESHOLD_CRITICAL
export TRR_THRESHOLD_NONCRITICAL
export PARA_P_UNIFORM
export PARA_P_CRITICAL
export PARA_P_NONCRITICAL
export CRITICAL_ROW_STRIDE
export CRITICAL_ROW_OFFSET
export FLIP_MODE
export FLIP_THRESHOLD
export PROB_FUNCTION
export PROB_FLIP_START
export PROB_ALPHA
export PROB_K
export PROB_MIDPOINT
export RANDOM_SEED
export BUDGET_WINDOW
export BUDGET_PER_WINDOW
export DEBUG

envsubst <"${TEMPLATE}" >"${CONFIG_FILE}"

echo "========================================"
echo "[run_case] CASE_NAME = ${CASE_NAME}"
echo "[run_case] CONFIG    = ${CONFIG_FILE}"
echo "[run_case] LOG      = ${LOG_FILE}"
echo "========================================"

"${ROOT_DIR}/build/ramulator2" -f "${CONFIG_FILE}" | tee "${LOG_FILE}"
