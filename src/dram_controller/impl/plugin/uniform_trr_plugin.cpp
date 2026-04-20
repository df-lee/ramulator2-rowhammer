#include <cmath>
#include <cstdint>
#include <deque>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "base/base.h"
#include "base/request.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"

namespace Ramulator {

class UniformTRRPlugin final : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(
    IControllerPlugin,
    UniformTRRPlugin,
    "UniformTRR",
    "Timing-aware RowHammer defense plugin using row-level ACT/PRE surrogate for TRR/PARA."
  );

private:
  enum class ProtectionFamily {
    TRR,
    PARA
  };

  enum class ProtectionScope {
    Uniform,
    SecurityAware
  };

  enum class FlipMode {
    Deterministic,
    Probabilistic
  };

  enum class ProbFunction {
    Linear,
    Sigmoid,
    Step
  };

  enum class PendingStage {
    NeedOpen,
    NeedClose
  };

  struct PendingProtection {
    AddrVec_t target_addr_vec {};
    int victim_row = -1;
    int aggressor_row = -1;
    bool is_trr = true;
    bool is_critical = false;
    PendingStage stage = PendingStage::NeedOpen;
  };

private:
  IDRAM* m_dram = nullptr;

  int m_row_level_idx = -1;
  int m_rank_level_idx = -1;

  int m_open_req_type = -1;
  int m_close_req_type = -1;

  int m_num_rows = 0;

  // Optional row-space compression for denser interaction under sparse traces
  bool m_compress_row_space = false;

  // Layer 1: aggressor-side hammer threshold
  int m_hammer_threshold = 16;

  // Protection policy selection
  ProtectionFamily m_protection_family = ProtectionFamily::TRR;
  ProtectionScope m_protection_scope = ProtectionScope::Uniform;

  bool m_debug = false;

  // TRR knobs
  int m_trr_threshold = 3;                 // uniform TRR
  int m_trr_threshold_critical = 2;        // security-aware TRR
  int m_trr_threshold_noncritical = 4;     // security-aware TRR

  // PARA knobs
  double m_para_p_uniform = 0.01;
  double m_para_p_critical = 0.10;
  double m_para_p_noncritical = 0.01;

  // Critical-row labeling knobs
  int m_critical_row_stride = 16;
  int m_critical_row_offset = 0;

  // Flip model knobs
  FlipMode m_flip_mode = FlipMode::Deterministic;
  ProbFunction m_prob_function = ProbFunction::Linear;

  int m_flip_threshold = 4;       // deterministic mode only

  int m_prob_flip_start = 2;      // probabilistic mode only
  double m_prob_alpha = 0.2;      // linear
  double m_prob_k = 1.0;          // sigmoid
  double m_prob_midpoint = 4.0;   // sigmoid

  uint64_t m_random_seed = 12345;
  std::mt19937_64 m_rng;
  std::uniform_real_distribution<double> m_unit_dist{0.0, 1.0};

  // Kept only for compatibility with old configs / summary scripts.
  // Fixed budget is disabled in this version.
  uint64_t m_budget_window = 1000;
  uint64_t m_budget_per_window = 32;

  // Fault-model state
  std::vector<int> m_act_count;
  std::vector<int> m_disturbance;

  // Pending row-level maintenance actions
  std::deque<PendingProtection> m_pending_protections;

  // Metrics
  size_t s_num_openings = 0;
  size_t s_num_hammer_events = 0;

  size_t s_num_trr_events = 0;
  size_t s_num_para_events = 0;

  size_t s_num_budget_blocks = 0;   // unused, kept for CSV compatibility
  size_t s_num_budget_resets = 0;   // unused, kept for CSV compatibility

  size_t s_num_bitflips = 0;

  size_t s_num_prob_checks = 0;
  size_t s_num_prob_successes = 0;

  size_t s_num_para_checks = 0;
  size_t s_num_para_successes = 0;

  size_t s_num_critical_bitflips = 0;
  size_t s_num_noncritical_bitflips = 0;

  size_t s_num_critical_trr_events = 0;
  size_t s_num_noncritical_trr_events = 0;

  size_t s_num_critical_para_events = 0;
  size_t s_num_noncritical_para_events = 0;

public:
  void init() override {
    m_num_rows = param<int>("num_rows")
      .desc("Number of rows in the simplified logical RowHammer model.")
      .default_val(32768);

    m_compress_row_space = param<bool>("compress_row_space")
      .desc("If true, map raw row IDs into [0, num_rows-1] by modulo.")
      .default_val(false);

    m_hammer_threshold = param<int>("hammer_threshold")
      .desc("Number of ACT/opening events on an aggressor row needed to form one hammer event.")
      .default_val(16);

    m_debug = param<bool>("debug")
      .desc("Print concise debug messages.")
      .default_val(false);

    parse_protection_family(
      param<std::string>("protection_family")
        .desc("Protection family: trr | para")
        .default_val("trr")
    );

    parse_protection_scope(
      param<std::string>("protection_scope")
        .desc("Protection scope: uniform | security_aware")
        .default_val("uniform")
    );

    m_trr_threshold = param<int>("trr_threshold")
      .desc("Victim disturbance threshold for uniform TRR.")
      .default_val(3);

    m_trr_threshold_critical = param<int>("trr_threshold_critical")
      .desc("Victim disturbance threshold for critical rows under security-aware TRR.")
      .default_val(2);

    m_trr_threshold_noncritical = param<int>("trr_threshold_noncritical")
      .desc("Victim disturbance threshold for non-critical rows under security-aware TRR.")
      .default_val(4);

    m_para_p_uniform = param<double>("para_p_uniform")
      .desc("Uniform PARA probability for each adjacent victim.")
      .default_val(0.01);

    m_para_p_critical = param<double>("para_p_critical")
      .desc("Security-aware PARA probability for critical victims.")
      .default_val(0.10);

    m_para_p_noncritical = param<double>("para_p_noncritical")
      .desc("Security-aware PARA probability for non-critical victims.")
      .default_val(0.01);

    m_critical_row_stride = param<int>("critical_row_stride")
      .desc("Stride for marking critical rows.")
      .default_val(16);

    m_critical_row_offset = param<int>("critical_row_offset")
      .desc("Offset for marking critical rows.")
      .default_val(0);

    parse_flip_mode(
      param<std::string>("flip_mode")
        .desc("Flip mode: deterministic | probabilistic")
        .default_val("deterministic")
    );

    parse_prob_function(
      param<std::string>("prob_function")
        .desc("Probability function: linear | sigmoid | step")
        .default_val("linear")
    );

    m_flip_threshold = param<int>("flip_threshold")
      .desc("Victim disturbance threshold for deterministic flip.")
      .default_val(4);

    m_prob_flip_start = param<int>("prob_flip_start")
      .desc("Minimum disturbance required before probabilistic flip checks start.")
      .default_val(2);

    m_prob_alpha = param<double>("prob_alpha")
      .desc("Slope for linear probabilistic flip model.")
      .default_val(0.2);

    m_prob_k = param<double>("prob_k")
      .desc("Slope for sigmoid probabilistic flip model.")
      .default_val(1.0);

    m_prob_midpoint = param<double>("prob_midpoint")
      .desc("Midpoint for sigmoid probabilistic flip model.")
      .default_val(4.0);

    m_random_seed = param<uint64_t>("random_seed")
      .desc("Random seed used by probabilistic flip and PARA.")
      .default_val(12345);

    // Deprecated; accepted only so old configs keep working
    m_budget_window = param<uint64_t>("budget_window")
      .desc("Deprecated in row-level timing-aware version; kept for config compatibility.")
      .default_val(1000);

    m_budget_per_window = param<uint64_t>("budget_per_window")
      .desc("Deprecated in row-level timing-aware version; kept for config compatibility.")
      .default_val(32);

    validate_params();

    m_rng.seed(m_random_seed);

    m_act_count.assign(m_num_rows, 0);
    m_disturbance.assign(m_num_rows, 0);
  }

  void setup(IFrontEnd*, IMemorySystem*) override {
    m_ctrl = cast_parent<IDRAMController>();
    if (m_ctrl == nullptr) {
      throw ConfigurationError("UniformTRR: failed to get parent controller.");
    }

    m_dram = m_ctrl->m_dram;
    if (m_dram == nullptr) {
      throw ConfigurationError("UniformTRR: controller DRAM pointer is null.");
    }

    m_row_level_idx = m_dram->m_levels("row");
    if (m_row_level_idx < 0) {
      throw ConfigurationError("UniformTRR: DRAM row level not found.");
    }

    m_rank_level_idx = m_dram->m_levels("rank");
    if (m_rank_level_idx < 0) {
      throw ConfigurationError("UniformTRR: DRAM rank level not found.");
    }

    m_open_req_type = m_dram->m_requests("open-row");
    if (m_open_req_type < 0) {
      throw ConfigurationError("UniformTRR: DRAM request type 'open-row' not found.");
    }

    m_close_req_type = m_dram->m_requests("close-row");
    if (m_close_req_type < 0) {
      throw ConfigurationError("UniformTRR: DRAM request type 'close-row' not found.");
    }

    log_setup();
  }

  void update(bool request_found, ReqBuffer::iterator& req_it) override {
    // First try to advance any pending row-level maintenance sequence.
    drain_pending_protections();

    if (!request_found || m_dram == nullptr) {
      return;
    }

    if (!is_row_opening_request(req_it)) {
      return;
    }

    s_num_openings++;

    const int raw_row = get_row(req_it);
    const int aggressor_row = map_row(raw_row);

    if (!is_valid_row(aggressor_row)) {
      return;
    }

    m_act_count[aggressor_row]++;

    if (m_act_count[aggressor_row] < m_hammer_threshold) {
      return;
    }

    m_act_count[aggressor_row] = 0;
    s_num_hammer_events++;

    log_hammer_event(aggressor_row);

    process_hammer_event(aggressor_row, req_it->addr_vec);
  }

  void finalize() override {
    std::cout
      << "[UniformTRR][summary] "
      << "openings=" << s_num_openings
      << " hammer_events=" << s_num_hammer_events
      << " trr_events=" << s_num_trr_events
      << " para_events=" << s_num_para_events
      << " bitflips=" << s_num_bitflips
      << " critical_bitflips=" << s_num_critical_bitflips
      << " noncritical_bitflips=" << s_num_noncritical_bitflips
      << " critical_trr_events=" << s_num_critical_trr_events
      << " noncritical_trr_events=" << s_num_noncritical_trr_events
      << " critical_para_events=" << s_num_critical_para_events
      << " noncritical_para_events=" << s_num_noncritical_para_events
      << " budget_blocks=" << s_num_budget_blocks
      << " budget_resets=" << s_num_budget_resets
      << " prob_checks=" << s_num_prob_checks
      << " prob_successes=" << s_num_prob_successes
      << " para_checks=" << s_num_para_checks
      << " para_successes=" << s_num_para_successes
      << " budget_used_final_window=0/" << m_budget_per_window
      << std::endl;
  }

private:
  void process_hammer_event(int aggressor_row, const AddrVec_t& trigger_addr_vec) {
    process_victim(aggressor_row - 1, aggressor_row, trigger_addr_vec);
    process_victim(aggressor_row + 1, aggressor_row, trigger_addr_vec);
  }

  void process_victim(int victim_row, int aggressor_row, const AddrVec_t& trigger_addr_vec) {
    if (!is_valid_row(victim_row)) {
      return;
    }

    m_disturbance[victim_row]++;

    if (try_protection(victim_row, aggressor_row, trigger_addr_vec)) {
      return;
    }

    apply_flip_model(victim_row, aggressor_row);
  }

  bool try_protection(int victim_row, int aggressor_row, const AddrVec_t& trigger_addr_vec) {
    switch (m_protection_family) {
      case ProtectionFamily::TRR:
        switch (m_protection_scope) {
          case ProtectionScope::Uniform:
            return try_uniform_trr(victim_row, aggressor_row, trigger_addr_vec);
          case ProtectionScope::SecurityAware:
            return try_security_aware_trr(victim_row, aggressor_row, trigger_addr_vec);
        }
        break;

      case ProtectionFamily::PARA:
        switch (m_protection_scope) {
          case ProtectionScope::Uniform:
            return try_uniform_para(victim_row, aggressor_row, trigger_addr_vec);
          case ProtectionScope::SecurityAware:
            return try_security_aware_para(victim_row, aggressor_row, trigger_addr_vec);
        }
        break;
    }

    return false;
  }

  bool try_uniform_trr(int victim_row, int aggressor_row, const AddrVec_t& trigger_addr_vec) {
    if (m_disturbance[victim_row] < m_trr_threshold) {
      return false;
    }

    enqueue_pending_protection(
      victim_row,
      aggressor_row,
      true,
      is_critical_row(victim_row),
      trigger_addr_vec
    );
    return true;
  }

  bool try_security_aware_trr(int victim_row, int aggressor_row, const AddrVec_t& trigger_addr_vec) {
    const bool critical = is_critical_row(victim_row);
    const int threshold = critical ? m_trr_threshold_critical
                                   : m_trr_threshold_noncritical;

    if (m_disturbance[victim_row] < threshold) {
      return false;
    }

    enqueue_pending_protection(
      victim_row,
      aggressor_row,
      true,
      critical,
      trigger_addr_vec
    );
    return true;
  }

  bool try_uniform_para(int victim_row, int aggressor_row, const AddrVec_t& trigger_addr_vec) {
    const double p = clamp_probability(m_para_p_uniform);
    const double u = m_unit_dist(m_rng);

    s_num_para_checks++;

    if (u < p) {
      s_num_para_successes++;
      enqueue_pending_protection(
        victim_row,
        aggressor_row,
        false,
        is_critical_row(victim_row),
        trigger_addr_vec
      );
      log_para_fire(victim_row, aggressor_row, p, u, is_critical_row(victim_row));
      return true;
    }

    return false;
  }

  bool try_security_aware_para(int victim_row, int aggressor_row, const AddrVec_t& trigger_addr_vec) {
    const bool critical = is_critical_row(victim_row);
    const double p = critical ? clamp_probability(m_para_p_critical)
                              : clamp_probability(m_para_p_noncritical);
    const double u = m_unit_dist(m_rng);

    s_num_para_checks++;

    if (u < p) {
      s_num_para_successes++;
      enqueue_pending_protection(
        victim_row,
        aggressor_row,
        false,
        critical,
        trigger_addr_vec
      );
      log_para_fire(victim_row, aggressor_row, p, u, critical);
      return true;
    }

    return false;
  }

  void apply_flip_model(int victim_row, int aggressor_row) {
    switch (m_flip_mode) {
      case FlipMode::Deterministic:
        maybe_bitflip_deterministic(victim_row, aggressor_row);
        return;
      case FlipMode::Probabilistic:
        maybe_bitflip_probabilistic(victim_row, aggressor_row);
        return;
    }
  }

  void maybe_bitflip_deterministic(int victim_row, int aggressor_row) {
    if (m_disturbance[victim_row] >= m_flip_threshold) {
      const bool critical = is_critical_row(victim_row);
      s_num_bitflips++;
      if (critical) {
        s_num_critical_bitflips++;
      } else {
        s_num_noncritical_bitflips++;
      }
      log_bitflip(victim_row, aggressor_row, -1.0, -1.0);
      m_disturbance[victim_row] = 0;
    }
  }

  void maybe_bitflip_probabilistic(int victim_row, int aggressor_row) {
    if (m_disturbance[victim_row] < m_prob_flip_start) {
      return;
    }

    const double p = compute_flip_probability(m_disturbance[victim_row]);
    const double u = m_unit_dist(m_rng);

    s_num_prob_checks++;

    if (u < p) {
      const bool critical = is_critical_row(victim_row);
      s_num_prob_successes++;
      s_num_bitflips++;
      if (critical) {
        s_num_critical_bitflips++;
      } else {
        s_num_noncritical_bitflips++;
      }
      log_bitflip(victim_row, aggressor_row, p, u);
      m_disturbance[victim_row] = 0;
    }
  }

  double compute_flip_probability(int disturbance) const {
    if (disturbance < m_prob_flip_start) {
      return 0.0;
    }

    switch (m_prob_function) {
      case ProbFunction::Linear: {
        const double p =
          m_prob_alpha * static_cast<double>(disturbance - m_prob_flip_start + 1);
        return clamp_probability(p);
      }

      case ProbFunction::Sigmoid: {
        const double x = static_cast<double>(disturbance) - m_prob_midpoint;
        const double p = 1.0 / (1.0 + std::exp(-m_prob_k * x));
        return clamp_probability(p);
      }

      case ProbFunction::Step:
        return 1.0;
    }

    return 0.0;
  }

private:
  void enqueue_pending_protection(
    int victim_row,
    int aggressor_row,
    bool is_trr,
    bool is_critical,
    const AddrVec_t& trigger_addr_vec
  ) {
    if (has_pending_for_victim(victim_row, is_trr, is_critical)) {
      return;
    }

    PendingProtection p;
    p.target_addr_vec = build_target_row_addr_vec(trigger_addr_vec, victim_row);
    p.victim_row = victim_row;
    p.aggressor_row = aggressor_row;
    p.is_trr = is_trr;
    p.is_critical = is_critical;
    p.stage = PendingStage::NeedOpen;
    m_pending_protections.push_back(p);
  }

  bool has_pending_for_victim(int victim_row, bool is_trr, bool is_critical) const {
    for (const auto& p : m_pending_protections) {
      if (p.victim_row == victim_row &&
          p.is_trr == is_trr &&
          p.is_critical == is_critical) {
        return true;
      }
    }
    return false;
  }

  void drain_pending_protections() {
    if (m_pending_protections.empty()) {
      return;
    }

    PendingProtection& p = m_pending_protections.front();

    if (p.stage == PendingStage::NeedOpen) {
      Request req(p.target_addr_vec, m_open_req_type);
      const bool ok = m_ctrl->priority_send(req);
      if (ok) {
        if (m_debug) {
          std::cout
            << "[UniformTRR][maint-open] "
            << "victim=" << p.victim_row
            << " aggressor=" << p.aggressor_row
            << " class=" << (p.is_critical ? "critical" : "noncritical")
            << std::endl;
        }
        p.stage = PendingStage::NeedClose;
      }
      return;
    }

    if (p.stage == PendingStage::NeedClose) {
      Request req(p.target_addr_vec, m_close_req_type);
      const bool ok = m_ctrl->priority_send(req);
      if (ok) {
        if (m_debug) {
          std::cout
            << "[UniformTRR][maint-close] "
            << "victim=" << p.victim_row
            << " aggressor=" << p.aggressor_row
            << " class=" << (p.is_critical ? "critical" : "noncritical")
            << std::endl;
        }

        commit_protection_effect(p.victim_row, p.aggressor_row, p.is_trr, p.is_critical);
        m_pending_protections.pop_front();
      }
      return;
    }
  }

  void commit_protection_effect(int victim_row, int aggressor_row, bool is_trr, bool is_critical) {
    if (!is_valid_row(victim_row)) {
      return;
    }

    m_disturbance[victim_row] = 0;

    if (is_trr) {
      s_num_trr_events++;
      if (is_critical) {
        s_num_critical_trr_events++;
      } else {
        s_num_noncritical_trr_events++;
      }
      log_trr_fire(victim_row, aggressor_row, is_critical);
    } else {
      s_num_para_events++;
      if (is_critical) {
        s_num_critical_para_events++;
      } else {
        s_num_noncritical_para_events++;
      }
    }
  }

  AddrVec_t build_target_row_addr_vec(const AddrVec_t& trigger_addr_vec, int victim_row) const {
    AddrVec_t addr_vec = trigger_addr_vec;

    if (m_row_level_idx >= 0 && m_row_level_idx < static_cast<int>(addr_vec.size())) {
      addr_vec[m_row_level_idx] = victim_row;
    }

    // Clear all levels below row so this acts as a row-level maintenance surrogate.
    for (int i = m_row_level_idx + 1; i < static_cast<int>(addr_vec.size()); i++) {
      addr_vec[i] = -1;
    }

    return addr_vec;
  }

  bool is_row_opening_request(ReqBuffer::iterator& req_it) const {
    if (!m_dram->m_command_meta(req_it->command).is_opening) {
      return false;
    }
    if (m_dram->m_command_scopes(req_it->command) != m_row_level_idx) {
      return false;
    }
    if (m_row_level_idx >= static_cast<int>(req_it->addr_vec.size())) {
      return false;
    }
    return true;
  }

  int get_row(ReqBuffer::iterator& req_it) const {
    return req_it->addr_vec[m_row_level_idx];
  }

  int map_row(int raw_row) const {
    if (!m_compress_row_space) {
      return raw_row;
    }

    if (m_num_rows <= 0) {
      return raw_row;
    }

    int mapped = raw_row % m_num_rows;
    if (mapped < 0) {
      mapped += m_num_rows;
    }
    return mapped;
  }

  bool is_valid_row(int row) const {
    return row >= 0 && row < m_num_rows;
  }

  bool is_critical_row(int row) const {
    if (m_critical_row_stride <= 0) {
      return false;
    }

    int shifted = row - m_critical_row_offset;
    int rem = shifted % m_critical_row_stride;
    if (rem < 0) {
      rem += m_critical_row_stride;
    }
    return rem == 0;
  }

  static double clamp_probability(double p) {
    if (p < 0.0) return 0.0;
    if (p > 1.0) return 1.0;
    return p;
  }

  void parse_protection_family(const std::string& s) {
    if (s == "trr") {
      m_protection_family = ProtectionFamily::TRR;
    } else if (s == "para") {
      m_protection_family = ProtectionFamily::PARA;
    } else {
      throw ConfigurationError("UniformTRR: protection_family must be 'trr' or 'para'.");
    }
  }

  void parse_protection_scope(const std::string& s) {
    if (s == "uniform") {
      m_protection_scope = ProtectionScope::Uniform;
    } else if (s == "security_aware") {
      m_protection_scope = ProtectionScope::SecurityAware;
    } else {
      throw ConfigurationError("UniformTRR: protection_scope must be 'uniform' or 'security_aware'.");
    }
  }

  void parse_flip_mode(const std::string& s) {
    if (s == "deterministic") {
      m_flip_mode = FlipMode::Deterministic;
    } else if (s == "probabilistic") {
      m_flip_mode = FlipMode::Probabilistic;
    } else {
      throw ConfigurationError("UniformTRR: flip_mode must be 'deterministic' or 'probabilistic'.");
    }
  }

  void parse_prob_function(const std::string& s) {
    if (s == "linear") {
      m_prob_function = ProbFunction::Linear;
    } else if (s == "sigmoid") {
      m_prob_function = ProbFunction::Sigmoid;
    } else if (s == "step") {
      m_prob_function = ProbFunction::Step;
    } else {
      throw ConfigurationError("UniformTRR: prob_function must be 'linear', 'sigmoid', or 'step'.");
    }
  }

  static const char* protection_family_name(ProtectionFamily f) {
    switch (f) {
      case ProtectionFamily::TRR: return "trr";
      case ProtectionFamily::PARA: return "para";
    }
    return "unknown";
  }

  static const char* protection_scope_name(ProtectionScope s) {
    switch (s) {
      case ProtectionScope::Uniform: return "uniform";
      case ProtectionScope::SecurityAware: return "security_aware";
    }
    return "unknown";
  }

  static const char* flip_mode_name(FlipMode mode) {
    switch (mode) {
      case FlipMode::Deterministic: return "deterministic";
      case FlipMode::Probabilistic: return "probabilistic";
    }
    return "unknown";
  }

  static const char* prob_function_name(ProbFunction fn) {
    switch (fn) {
      case ProbFunction::Linear: return "linear";
      case ProbFunction::Sigmoid: return "sigmoid";
      case ProbFunction::Step: return "step";
    }
    return "unknown";
  }

  void validate_params() {
    if (m_num_rows <= 0) {
      throw ConfigurationError("UniformTRR: num_rows must be positive.");
    }
    if (m_hammer_threshold <= 0) {
      throw ConfigurationError("UniformTRR: hammer_threshold must be positive.");
    }
    if (m_flip_threshold <= 0) {
      throw ConfigurationError("UniformTRR: flip_threshold must be positive.");
    }
    if (m_prob_flip_start <= 0) {
      throw ConfigurationError("UniformTRR: prob_flip_start must be positive.");
    }
    if (m_prob_alpha < 0.0) {
      throw ConfigurationError("UniformTRR: prob_alpha must be non-negative.");
    }
    if (m_prob_k <= 0.0) {
      throw ConfigurationError("UniformTRR: prob_k must be positive.");
    }
    if (m_trr_threshold <= 0 ||
        m_trr_threshold_critical <= 0 ||
        m_trr_threshold_noncritical <= 0) {
      throw ConfigurationError("UniformTRR: all TRR thresholds must be positive.");
    }
    if (m_critical_row_stride <= 0) {
      throw ConfigurationError("UniformTRR: critical_row_stride must be positive.");
    }

    if (m_para_p_uniform < 0.0 || m_para_p_uniform > 1.0 ||
        m_para_p_critical < 0.0 || m_para_p_critical > 1.0 ||
        m_para_p_noncritical < 0.0 || m_para_p_noncritical > 1.0) {
      throw ConfigurationError("UniformTRR: PARA probabilities must be in [0, 1].");
    }
  }

  void log_setup() const {
    if (!m_debug) {
      return;
    }

    std::cout
      << "[UniformTRR][setup] "
      << "row_level_idx=" << m_row_level_idx
      << " rank_level_idx=" << m_rank_level_idx
      << " open_req_type=" << m_open_req_type
      << " close_req_type=" << m_close_req_type
      << " family=" << protection_family_name(m_protection_family)
      << " scope=" << protection_scope_name(m_protection_scope)
      << " compress_row_space=" << (m_compress_row_space ? "true" : "false")
      << " num_rows=" << m_num_rows
      << " hammer_threshold=" << m_hammer_threshold
      << " trr_threshold=" << m_trr_threshold
      << " trr_threshold_critical=" << m_trr_threshold_critical
      << " trr_threshold_noncritical=" << m_trr_threshold_noncritical
      << " para_p_uniform=" << m_para_p_uniform
      << " para_p_critical=" << m_para_p_critical
      << " para_p_noncritical=" << m_para_p_noncritical
      << " flip_mode=" << flip_mode_name(m_flip_mode)
      << " prob_function=" << prob_function_name(m_prob_function)
      << " prob_flip_start=" << m_prob_flip_start
      << " prob_alpha=" << m_prob_alpha
      << " prob_k=" << m_prob_k
      << " prob_midpoint=" << m_prob_midpoint
      << " critical_row_stride=" << m_critical_row_stride
      << " critical_row_offset=" << m_critical_row_offset
      << " fixed_budget=disabled"
      << " random_seed=" << m_random_seed
      << std::endl;
  }

  void log_hammer_event(int aggressor_row) const {
    if (!m_debug) {
      return;
    }

    std::cout
      << "[UniformTRR][hammer] "
      << "aggressor=" << aggressor_row
      << " victims=(" << aggressor_row - 1
      << "," << aggressor_row + 1 << ")"
      << std::endl;
  }

  void log_trr_fire(int victim_row, int aggressor_row, bool critical) const {
    if (!m_debug) {
      return;
    }

    std::cout
      << "[UniformTRR][trr] "
      << "victim=" << victim_row
      << " aggressor=" << aggressor_row
      << " class=" << (critical ? "critical" : "noncritical")
      << " mode=row-level-v1"
      << std::endl;
  }

  void log_para_fire(int victim_row, int aggressor_row, double p, double u, bool critical) const {
    if (!m_debug) {
      return;
    }

    std::cout
      << "[UniformTRR][para] "
      << "victim=" << victim_row
      << " aggressor=" << aggressor_row
      << " class=" << (critical ? "critical" : "noncritical")
      << " p=" << p
      << " sample=" << u
      << " mode=row-level-v1"
      << std::endl;
  }

  void log_bitflip(int victim_row, int aggressor_row, double p, double u) const {
    if (!m_debug) {
      return;
    }

    std::cout
      << "[UniformTRR][bitflip] "
      << "victim=" << victim_row
      << " aggressor=" << aggressor_row
      << " class=" << (is_critical_row(victim_row) ? "critical" : "noncritical")
      << " disturbance=" << m_disturbance[victim_row];

    if (m_flip_mode == FlipMode::Probabilistic) {
      std::cout << " p=" << p << " sample=" << u;
    }

    std::cout << std::endl;
  }
};

} // namespace Ramulator
