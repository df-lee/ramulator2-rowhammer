#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="${ROOT_DIR}/logs"
OUT_FILE="${ROOT_DIR}/summary.csv"

HEADER="case,openings,hammer_events,trr_events,para_events,bitflips,critical_bitflips,noncritical_bitflips,critical_trr_events,noncritical_trr_events,critical_para_events,noncritical_para_events,budget_blocks,budget_resets,prob_checks,prob_successes,para_checks,para_successes,budget_used_final_window,memory_system_cycles,num_read_reqs_0,read_latency_0,avg_read_latency_0,queue_len_avg_0,read_queue_len_avg_0,priority_queue_len_avg_0,row_conflicts_0,read_row_conflicts_0,row_misses_0,read_row_misses_0,row_hits_0,read_row_hits_0"

echo "${HEADER}" >"${OUT_FILE}"

extract_summary_field() {
  local key="$1"
  local line="$2"

  awk -v target="${key}" '
    {
      for (i = 1; i <= NF; i++) {
        split($i, kv, "=")
        if (kv[1] == target) {
          print kv[2]
          exit
        }
      }
    }
  ' <<<"${line}"
}

extract_stat_field() {
  local key="$1"
  local file="$2"

  awk -F': ' -v target="${key}" '
    $1 ~ "^[[:space:]]*" target "$" {
      gsub(/^[[:space:]]+/, "", $2)
      print $2
      exit
    }
  ' "${file}"
}

for LOG in "${LOG_DIR}"/*.log; do
  [[ -e "$LOG" ]] || continue

  CASE_NAME="$(basename "$LOG" .log)"
  SUMMARY_LINE="$(grep '\[UniformTRR\]\[summary\]' "$LOG" | tail -n 1 || true)"

  if [[ -z "${SUMMARY_LINE}" ]]; then
    continue
  fi

  openings="$(extract_summary_field openings "${SUMMARY_LINE}")"
  hammer_events="$(extract_summary_field hammer_events "${SUMMARY_LINE}")"
  trr_events="$(extract_summary_field trr_events "${SUMMARY_LINE}")"
  para_events="$(extract_summary_field para_events "${SUMMARY_LINE}")"
  bitflips="$(extract_summary_field bitflips "${SUMMARY_LINE}")"

  critical_bitflips="$(extract_summary_field critical_bitflips "${SUMMARY_LINE}")"
  noncritical_bitflips="$(extract_summary_field noncritical_bitflips "${SUMMARY_LINE}")"

  critical_trr_events="$(extract_summary_field critical_trr_events "${SUMMARY_LINE}")"
  noncritical_trr_events="$(extract_summary_field noncritical_trr_events "${SUMMARY_LINE}")"

  critical_para_events="$(extract_summary_field critical_para_events "${SUMMARY_LINE}")"
  noncritical_para_events="$(extract_summary_field noncritical_para_events "${SUMMARY_LINE}")"

  budget_blocks="$(extract_summary_field budget_blocks "${SUMMARY_LINE}")"
  budget_resets="$(extract_summary_field budget_resets "${SUMMARY_LINE}")"

  prob_checks="$(extract_summary_field prob_checks "${SUMMARY_LINE}")"
  prob_successes="$(extract_summary_field prob_successes "${SUMMARY_LINE}")"

  para_checks="$(extract_summary_field para_checks "${SUMMARY_LINE}")"
  para_successes="$(extract_summary_field para_successes "${SUMMARY_LINE}")"

  budget_used_final_window="$(extract_summary_field budget_used_final_window "${SUMMARY_LINE}")"

  memory_system_cycles="$(extract_stat_field memory_system_cycles "${LOG}")"
  num_read_reqs_0="$(extract_stat_field num_read_reqs_0 "${LOG}")"
  read_latency_0="$(extract_stat_field read_latency_0 "${LOG}")"
  avg_read_latency_0="$(extract_stat_field avg_read_latency_0 "${LOG}")"
  queue_len_avg_0="$(extract_stat_field queue_len_avg_0 "${LOG}")"
  read_queue_len_avg_0="$(extract_stat_field read_queue_len_avg_0 "${LOG}")"
  priority_queue_len_avg_0="$(extract_stat_field priority_queue_len_avg_0 "${LOG}")"
  row_conflicts_0="$(extract_stat_field row_conflicts_0 "${LOG}")"
  read_row_conflicts_0="$(extract_stat_field read_row_conflicts_0 "${LOG}")"
  row_misses_0="$(extract_stat_field row_misses_0 "${LOG}")"
  read_row_misses_0="$(extract_stat_field read_row_misses_0 "${LOG}")"
  row_hits_0="$(extract_stat_field row_hits_0 "${LOG}")"
  read_row_hits_0="$(extract_stat_field read_row_hits_0 "${LOG}")"

  for var_name in \
    openings hammer_events trr_events para_events bitflips \
    critical_bitflips noncritical_bitflips \
    critical_trr_events noncritical_trr_events \
    critical_para_events noncritical_para_events \
    budget_blocks budget_resets prob_checks prob_successes \
    para_checks para_successes budget_used_final_window \
    memory_system_cycles num_read_reqs_0 read_latency_0 avg_read_latency_0 \
    queue_len_avg_0 read_queue_len_avg_0 priority_queue_len_avg_0 \
    row_conflicts_0 read_row_conflicts_0 row_misses_0 read_row_misses_0 \
    row_hits_0 read_row_hits_0; do
    if [[ -z "${!var_name:-}" ]]; then
      printf -v "${var_name}" '%s' "NA"
    fi
  done

  echo "${CASE_NAME},${openings},${hammer_events},${trr_events},${para_events},${bitflips},${critical_bitflips},${noncritical_bitflips},${critical_trr_events},${noncritical_trr_events},${critical_para_events},${noncritical_para_events},${budget_blocks},${budget_resets},${prob_checks},${prob_successes},${para_checks},${para_successes},${budget_used_final_window},${memory_system_cycles},${num_read_reqs_0},${read_latency_0},${avg_read_latency_0},${queue_len_avg_0},${read_queue_len_avg_0},${priority_queue_len_avg_0},${row_conflicts_0},${read_row_conflicts_0},${row_misses_0},${read_row_misses_0},${row_hits_0},${read_row_hits_0}" >>"${OUT_FILE}"
done

echo "Summary written to ${OUT_FILE}"
