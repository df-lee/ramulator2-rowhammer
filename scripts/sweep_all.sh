#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_SCRIPT="${ROOT_DIR}/scripts/run_case.sh"

# Use absolute path so running from scripts/ or project root both work.
TRACE="${ROOT_DIR}/example_rh_physaddr.trace"

run_case() {
  local case_name="$1"
  shift
  env CASE_NAME="${case_name}" TRACE="${TRACE}" "$@" "${RUN_SCRIPT}"
}

prob_tag() {
  local p="$1"
  # 0.01 -> p0p01, 0.10 -> p0p10
  printf "%s" "${p}" | sed 's/\./p/g'
}

# --------------------------------------------------
# Representative fault settings
# --------------------------------------------------
FAULT_SETTINGS=(
  "h1_ps1_sigmoid_nr64:1:1:sigmoid:64"
  "h1_ps1_sigmoid_nr128:1:1:sigmoid:128"
  "h1_ps1_linear_nr64:1:1:linear:64"
  "h1_ps2_sigmoid_nr64:1:2:sigmoid:64"
)

# --------------------------------------------------
# Common fixed knobs
# --------------------------------------------------
COMPRESS_ROW_SPACE=true

CRITICAL_ROW_STRIDE=16
CRITICAL_ROW_OFFSET=0
RANDOM_SEED=12345
DEBUG=false

# Fixed-budget knobs kept only for config compatibility.
BUDGET_WINDOW=1000
BUDGET_PER_WINDOW=32

# Flip model defaults
FLIP_MODE=probabilistic
FLIP_THRESHOLD=4
PROB_ALPHA=0.2
PROB_K=1.0
PROB_MIDPOINT=4.0

# --------------------------------------------------
# Security-aware TRR parameter grid (stabilized)
# --------------------------------------------------
TRR_THRESHOLD_UNIFORM=3
TRR_CRITICAL_LIST=(2 3 4 5)
TRR_NONCRITICAL_LIST=(2 3 4 5 6 7 8 9 10)

# --------------------------------------------------
# Security-aware PARA parameter grid (stabilized)
# --------------------------------------------------
PARA_P_UNIFORM=0.01
PARA_PROB_LIST=(
  0.01 0.02 0.03 0.04 0.05
  0.06 0.07 0.08 0.09 0.10
  0.12 0.14 0.16 0.18 0.20
)

# --------------------------------------------------
# Main sweep
# --------------------------------------------------
for ENTRY in "${FAULT_SETTINGS[@]}"; do
  IFS=':' read -r FAULT_TAG HAMMER PSTART PFUNC NROWS <<<"${ENTRY}"

  echo "============================================================"
  echo "Running fault setting: ${FAULT_TAG}"
  echo "  hammer_threshold = ${HAMMER}"
  echo "  prob_flip_start  = ${PSTART}"
  echo "  prob_function    = ${PFUNC}"
  echo "  num_rows         = ${NROWS}"
  echo "============================================================"

  # ------------------------------------------------
  # A) TRR grid sweep
  # ------------------------------------------------
  for TC in "${TRR_CRITICAL_LIST[@]}"; do
    for TN in "${TRR_NONCRITICAL_LIST[@]}"; do

      # Uniform TRR baseline: run once per fault setting
      if [[ "${TC}" == "${TRR_CRITICAL_LIST[0]}" && "${TN}" == "${TRR_NONCRITICAL_LIST[0]}" ]]; then
        run_case "trr_uniform_${FAULT_TAG}" \
          PROTECTION_FAMILY=trr \
          PROTECTION_SCOPE=uniform \
          NUM_ROWS="${NROWS}" \
          COMPRESS_ROW_SPACE="${COMPRESS_ROW_SPACE}" \
          HAMMER_THRESHOLD="${HAMMER}" \
          TRR_THRESHOLD="${TRR_THRESHOLD_UNIFORM}" \
          TRR_THRESHOLD_CRITICAL=2 \
          TRR_THRESHOLD_NONCRITICAL=4 \
          PARA_P_UNIFORM="${PARA_P_UNIFORM}" \
          PARA_P_CRITICAL=0.10 \
          PARA_P_NONCRITICAL=0.01 \
          CRITICAL_ROW_STRIDE="${CRITICAL_ROW_STRIDE}" \
          CRITICAL_ROW_OFFSET="${CRITICAL_ROW_OFFSET}" \
          FLIP_MODE="${FLIP_MODE}" \
          FLIP_THRESHOLD="${FLIP_THRESHOLD}" \
          PROB_FUNCTION="${PFUNC}" \
          PROB_FLIP_START="${PSTART}" \
          PROB_ALPHA="${PROB_ALPHA}" \
          PROB_K="${PROB_K}" \
          PROB_MIDPOINT="${PROB_MIDPOINT}" \
          RANDOM_SEED="${RANDOM_SEED}" \
          BUDGET_WINDOW="${BUDGET_WINDOW}" \
          BUDGET_PER_WINDOW="${BUDGET_PER_WINDOW}" \
          DEBUG="${DEBUG}"
      fi

      run_case "trr_sec_${FAULT_TAG}_tc${TC}_tn${TN}" \
        PROTECTION_FAMILY=trr \
        PROTECTION_SCOPE=security_aware \
        NUM_ROWS="${NROWS}" \
        COMPRESS_ROW_SPACE="${COMPRESS_ROW_SPACE}" \
        HAMMER_THRESHOLD="${HAMMER}" \
        TRR_THRESHOLD="${TRR_THRESHOLD_UNIFORM}" \
        TRR_THRESHOLD_CRITICAL="${TC}" \
        TRR_THRESHOLD_NONCRITICAL="${TN}" \
        PARA_P_UNIFORM="${PARA_P_UNIFORM}" \
        PARA_P_CRITICAL=0.10 \
        PARA_P_NONCRITICAL=0.01 \
        CRITICAL_ROW_STRIDE="${CRITICAL_ROW_STRIDE}" \
        CRITICAL_ROW_OFFSET="${CRITICAL_ROW_OFFSET}" \
        FLIP_MODE="${FLIP_MODE}" \
        FLIP_THRESHOLD="${FLIP_THRESHOLD}" \
        PROB_FUNCTION="${PFUNC}" \
        PROB_FLIP_START="${PSTART}" \
        PROB_ALPHA="${PROB_ALPHA}" \
        PROB_K="${PROB_K}" \
        PROB_MIDPOINT="${PROB_MIDPOINT}" \
        RANDOM_SEED="${RANDOM_SEED}" \
        BUDGET_WINDOW="${BUDGET_WINDOW}" \
        BUDGET_PER_WINDOW="${BUDGET_PER_WINDOW}" \
        DEBUG="${DEBUG}"
    done
  done

  # ------------------------------------------------
  # B) PARA grid sweep
  # ------------------------------------------------
  for PC in "${PARA_PROB_LIST[@]}"; do
    for PN in "${PARA_PROB_LIST[@]}"; do
      PC_TAG="$(prob_tag "${PC}")"
      PN_TAG="$(prob_tag "${PN}")"

      # Uniform PARA baseline: run once per fault setting
      if [[ "${PC}" == "${PARA_PROB_LIST[0]}" && "${PN}" == "${PARA_PROB_LIST[0]}" ]]; then
        run_case "para_uniform_${FAULT_TAG}" \
          PROTECTION_FAMILY=para \
          PROTECTION_SCOPE=uniform \
          NUM_ROWS="${NROWS}" \
          COMPRESS_ROW_SPACE="${COMPRESS_ROW_SPACE}" \
          HAMMER_THRESHOLD="${HAMMER}" \
          TRR_THRESHOLD="${TRR_THRESHOLD_UNIFORM}" \
          TRR_THRESHOLD_CRITICAL=2 \
          TRR_THRESHOLD_NONCRITICAL=4 \
          PARA_P_UNIFORM="${PARA_P_UNIFORM}" \
          PARA_P_CRITICAL=0.10 \
          PARA_P_NONCRITICAL=0.01 \
          CRITICAL_ROW_STRIDE="${CRITICAL_ROW_STRIDE}" \
          CRITICAL_ROW_OFFSET="${CRITICAL_ROW_OFFSET}" \
          FLIP_MODE="${FLIP_MODE}" \
          FLIP_THRESHOLD="${FLIP_THRESHOLD}" \
          PROB_FUNCTION="${PFUNC}" \
          PROB_FLIP_START="${PSTART}" \
          PROB_ALPHA="${PROB_ALPHA}" \
          PROB_K="${PROB_K}" \
          PROB_MIDPOINT="${PROB_MIDPOINT}" \
          RANDOM_SEED="${RANDOM_SEED}" \
          BUDGET_WINDOW="${BUDGET_WINDOW}" \
          BUDGET_PER_WINDOW="${BUDGET_PER_WINDOW}" \
          DEBUG="${DEBUG}"
      fi

      run_case "para_sec_${FAULT_TAG}_pc${PC_TAG}_pn${PN_TAG}" \
        PROTECTION_FAMILY=para \
        PROTECTION_SCOPE=security_aware \
        NUM_ROWS="${NROWS}" \
        COMPRESS_ROW_SPACE="${COMPRESS_ROW_SPACE}" \
        HAMMER_THRESHOLD="${HAMMER}" \
        TRR_THRESHOLD="${TRR_THRESHOLD_UNIFORM}" \
        TRR_THRESHOLD_CRITICAL=2 \
        TRR_THRESHOLD_NONCRITICAL=4 \
        PARA_P_UNIFORM="${PARA_P_UNIFORM}" \
        PARA_P_CRITICAL="${PC}" \
        PARA_P_NONCRITICAL="${PN}" \
        CRITICAL_ROW_STRIDE="${CRITICAL_ROW_STRIDE}" \
        CRITICAL_ROW_OFFSET="${CRITICAL_ROW_OFFSET}" \
        FLIP_MODE="${FLIP_MODE}" \
        FLIP_THRESHOLD="${FLIP_THRESHOLD}" \
        PROB_FUNCTION="${PFUNC}" \
        PROB_FLIP_START="${PSTART}" \
        PROB_ALPHA="${PROB_ALPHA}" \
        PROB_K="${PROB_K}" \
        PROB_MIDPOINT="${PROB_MIDPOINT}" \
        RANDOM_SEED="${RANDOM_SEED}" \
        BUDGET_WINDOW="${BUDGET_WINDOW}" \
        BUDGET_PER_WINDOW="${BUDGET_PER_WINDOW}" \
        DEBUG="${DEBUG}"
    done
  done
done

echo "All sweep cases completed."
