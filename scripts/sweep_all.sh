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

# --------------------------------------------------
# Fault-model sweep axes
# --------------------------------------------------
HAMMER_THRESHOLDS=(1 2 3 4)
PROB_FLIP_STARTS=(1 2 3 4)

# Slightly fuller row-space sweep
NUM_ROWS_LIST=(1024 512 256 128 64)

# Fix the probability function to sigmoid
PROB_FUNCTION=sigmoid

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
# Fixed policy knobs for fault-model sweep
# Chosen from the best / most representative timing-aware settings
# --------------------------------------------------

# Uniform TRR
TRR_THRESHOLD_UNIFORM=3

# Security-aware TRR
TRR_THRESHOLD_CRITICAL=3
TRR_THRESHOLD_NONCRITICAL=10

# Uniform PARA
PARA_P_UNIFORM=0.01

# Security-aware PARA
PARA_P_CRITICAL=0.18
PARA_P_NONCRITICAL=0.02

# --------------------------------------------------
# Main sweep
# --------------------------------------------------
for HAMMER in "${HAMMER_THRESHOLDS[@]}"; do
  for PSTART in "${PROB_FLIP_STARTS[@]}"; do
    for NROWS in "${NUM_ROWS_LIST[@]}"; do

      FAULT_TAG="h${HAMMER}_ps${PSTART}_${PROB_FUNCTION}_nr${NROWS}"

      echo "============================================================"
      echo "Running fault setting: ${FAULT_TAG}"
      echo "  hammer_threshold = ${HAMMER}"
      echo "  prob_flip_start  = ${PSTART}"
      echo "  prob_function    = ${PROB_FUNCTION}"
      echo "  num_rows         = ${NROWS}"
      echo "============================================================"

      # ------------------------------------------------
      # 1) Uniform TRR
      # ------------------------------------------------
      run_case "trr_uniform_${FAULT_TAG}" \
        PROTECTION_FAMILY=trr \
        PROTECTION_SCOPE=uniform \
        NUM_ROWS="${NROWS}" \
        COMPRESS_ROW_SPACE="${COMPRESS_ROW_SPACE}" \
        HAMMER_THRESHOLD="${HAMMER}" \
        TRR_THRESHOLD="${TRR_THRESHOLD_UNIFORM}" \
        TRR_THRESHOLD_CRITICAL="${TRR_THRESHOLD_CRITICAL}" \
        TRR_THRESHOLD_NONCRITICAL="${TRR_THRESHOLD_NONCRITICAL}" \
        PARA_P_UNIFORM="${PARA_P_UNIFORM}" \
        PARA_P_CRITICAL="${PARA_P_CRITICAL}" \
        PARA_P_NONCRITICAL="${PARA_P_NONCRITICAL}" \
        CRITICAL_ROW_STRIDE="${CRITICAL_ROW_STRIDE}" \
        CRITICAL_ROW_OFFSET="${CRITICAL_ROW_OFFSET}" \
        FLIP_MODE="${FLIP_MODE}" \
        FLIP_THRESHOLD="${FLIP_THRESHOLD}" \
        PROB_FUNCTION="${PROB_FUNCTION}" \
        PROB_FLIP_START="${PSTART}" \
        PROB_ALPHA="${PROB_ALPHA}" \
        PROB_K="${PROB_K}" \
        PROB_MIDPOINT="${PROB_MIDPOINT}" \
        RANDOM_SEED="${RANDOM_SEED}" \
        BUDGET_WINDOW="${BUDGET_WINDOW}" \
        BUDGET_PER_WINDOW="${BUDGET_PER_WINDOW}" \
        DEBUG="${DEBUG}"

      # ------------------------------------------------
      # 2) Security-aware TRR
      # ------------------------------------------------
      run_case "trr_sec_${FAULT_TAG}" \
        PROTECTION_FAMILY=trr \
        PROTECTION_SCOPE=security_aware \
        NUM_ROWS="${NROWS}" \
        COMPRESS_ROW_SPACE="${COMPRESS_ROW_SPACE}" \
        HAMMER_THRESHOLD="${HAMMER}" \
        TRR_THRESHOLD="${TRR_THRESHOLD_UNIFORM}" \
        TRR_THRESHOLD_CRITICAL="${TRR_THRESHOLD_CRITICAL}" \
        TRR_THRESHOLD_NONCRITICAL="${TRR_THRESHOLD_NONCRITICAL}" \
        PARA_P_UNIFORM="${PARA_P_UNIFORM}" \
        PARA_P_CRITICAL="${PARA_P_CRITICAL}" \
        PARA_P_NONCRITICAL="${PARA_P_NONCRITICAL}" \
        CRITICAL_ROW_STRIDE="${CRITICAL_ROW_STRIDE}" \
        CRITICAL_ROW_OFFSET="${CRITICAL_ROW_OFFSET}" \
        FLIP_MODE="${FLIP_MODE}" \
        FLIP_THRESHOLD="${FLIP_THRESHOLD}" \
        PROB_FUNCTION="${PROB_FUNCTION}" \
        PROB_FLIP_START="${PSTART}" \
        PROB_ALPHA="${PROB_ALPHA}" \
        PROB_K="${PROB_K}" \
        PROB_MIDPOINT="${PROB_MIDPOINT}" \
        RANDOM_SEED="${RANDOM_SEED}" \
        BUDGET_WINDOW="${BUDGET_WINDOW}" \
        BUDGET_PER_WINDOW="${BUDGET_PER_WINDOW}" \
        DEBUG="${DEBUG}"

      # ------------------------------------------------
      # 3) Uniform PARA
      # ------------------------------------------------
      run_case "para_uniform_${FAULT_TAG}" \
        PROTECTION_FAMILY=para \
        PROTECTION_SCOPE=uniform \
        NUM_ROWS="${NROWS}" \
        COMPRESS_ROW_SPACE="${COMPRESS_ROW_SPACE}" \
        HAMMER_THRESHOLD="${HAMMER}" \
        TRR_THRESHOLD="${TRR_THRESHOLD_UNIFORM}" \
        TRR_THRESHOLD_CRITICAL="${TRR_THRESHOLD_CRITICAL}" \
        TRR_THRESHOLD_NONCRITICAL="${TRR_THRESHOLD_NONCRITICAL}" \
        PARA_P_UNIFORM="${PARA_P_UNIFORM}" \
        PARA_P_CRITICAL="${PARA_P_CRITICAL}" \
        PARA_P_NONCRITICAL="${PARA_P_NONCRITICAL}" \
        CRITICAL_ROW_STRIDE="${CRITICAL_ROW_STRIDE}" \
        CRITICAL_ROW_OFFSET="${CRITICAL_ROW_OFFSET}" \
        FLIP_MODE="${FLIP_MODE}" \
        FLIP_THRESHOLD="${FLIP_THRESHOLD}" \
        PROB_FUNCTION="${PROB_FUNCTION}" \
        PROB_FLIP_START="${PSTART}" \
        PROB_ALPHA="${PROB_ALPHA}" \
        PROB_K="${PROB_K}" \
        PROB_MIDPOINT="${PROB_MIDPOINT}" \
        RANDOM_SEED="${RANDOM_SEED}" \
        BUDGET_WINDOW="${BUDGET_WINDOW}" \
        BUDGET_PER_WINDOW="${BUDGET_PER_WINDOW}" \
        DEBUG="${DEBUG}"

      # ------------------------------------------------
      # 4) Security-aware PARA
      # ------------------------------------------------
      run_case "para_sec_${FAULT_TAG}" \
        PROTECTION_FAMILY=para \
        PROTECTION_SCOPE=security_aware \
        NUM_ROWS="${NROWS}" \
        COMPRESS_ROW_SPACE="${COMPRESS_ROW_SPACE}" \
        HAMMER_THRESHOLD="${HAMMER}" \
        TRR_THRESHOLD="${TRR_THRESHOLD_UNIFORM}" \
        TRR_THRESHOLD_CRITICAL="${TRR_THRESHOLD_CRITICAL}" \
        TRR_THRESHOLD_NONCRITICAL="${TRR_THRESHOLD_NONCRITICAL}" \
        PARA_P_UNIFORM="${PARA_P_UNIFORM}" \
        PARA_P_CRITICAL="${PARA_P_CRITICAL}" \
        PARA_P_NONCRITICAL="${PARA_P_NONCRITICAL}" \
        CRITICAL_ROW_STRIDE="${CRITICAL_ROW_STRIDE}" \
        CRITICAL_ROW_OFFSET="${CRITICAL_ROW_OFFSET}" \
        FLIP_MODE="${FLIP_MODE}" \
        FLIP_THRESHOLD="${FLIP_THRESHOLD}" \
        PROB_FUNCTION="${PROB_FUNCTION}" \
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

echo "All fault-model sweep cases completed."
