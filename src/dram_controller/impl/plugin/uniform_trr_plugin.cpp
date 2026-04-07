#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"

namespace Ramulator {

class UniformTRRPlugin final : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(
    IControllerPlugin,
    UniformTRRPlugin,
    "UniformTRR",
    "A two-level RowHammer fault model with budget-aware victim-side uniform TRR "
    "and configurable probabilistic flip functions."
  );

private:
  enum class FlipMode {
    Deterministic,
    Probabilistic
  };

  enum class ProbFunction {
    Linear,
    Sigmoid,
    Step
  };

private:
  IDRAM* m_dram = nullptr;

  int m_row_level_idx = -1;
  int m_num_rows = 0;

  // Layer 1: aggressor-side threshold
  int m_hammer_threshold = 16;

  // Layer 2: victim-side thresholds
  int m_trr_threshold = 3;
  int m_flip_threshold = 4;   // used only in deterministic mode

  bool m_enable_trr = false;
  bool m_debug = false;

  // Flip model knobs
  FlipMode m_flip_mode = FlipMode::Deterministic;
  ProbFunction m_prob_function = ProbFunction::Linear;

  int m_prob_flip_start = 2;   // disturbance below this => p = 0
  double m_prob_alpha = 0.2;   // for linear
  double m_prob_k = 1.0;       // for sigmoid
  double m_prob_midpoint = 4.0; // for sigmoid
  uint64_t m_random_seed = 12345;

  std::mt19937_64 m_rng;
  std::uniform_real_distribution<double> m_unit_dist{0.0, 1.0};

  // Budget model: global budget per fixed window
  uint64_t m_budget_window = 1000;
  uint64_t m_budget_per_window = 32;
  uint64_t m_tick = 0;
  uint64_t m_window_start_tick = 0;
  uint64_t m_budget_used = 0;

  std::vector<int> m_act_count;
  std::vector<int> m_disturbance;

  // Summary counters
  size_t s_num_openings = 0;
  size_t s_num_hammer_events = 0;
  size_t s_num_trr_events = 0;
  size_t s_num_bitflips = 0;
  size_t s_num_budget_blocks = 0;
  size_t s_num_budget_resets = 0;
  size_t s_num_prob_checks = 0;
  size_t s_num_prob_successes = 0;

public:
  void init() override {
    m_num_rows = param<int>("num_rows")
      .desc("Number of rows in the simplified RowHammer model.")
      .default_val(32768);

    m_hammer_threshold = param<int>("hammer_threshold")
      .desc("Number of ACT/opening events on an aggressor row needed to generate one disturbance event.")
      .default_val(16);

    m_trr_threshold = param<int>("trr_threshold")
      .desc("Victim disturbance threshold for triggering uniform TRR.")
      .default_val(3);

    m_flip_threshold = param<int>("flip_threshold")
      .desc("Victim disturbance threshold for deterministic bit flip.")
      .default_val(4);

    m_enable_trr = param<bool>("enable_trr")
      .desc("Enable uniform TRR.")
      .default_val(false);

    m_debug = param<bool>("debug")
      .desc("Print concise debug messages.")
      .default_val(false);

    // Flip mode knobs
    const std::string flip_mode_str = param<std::string>("flip_mode")
      .desc("Flip mode: deterministic | probabilistic")
      .default_val("deterministic");

    const std::string prob_function_str = param<std::string>("prob_function")
      .desc("Probability function: linear | sigmoid | step")
      .default_val("linear");

    m_prob_flip_start = param<int>("prob_flip_start")
      .desc("Minimum disturbance required before probabilistic flips are considered.")
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
      .desc("Random seed for probabilistic flip mode.")
      .default_val(12345);

    // Budget knobs
    m_budget_window = param<uint64_t>("budget_window")
      .desc("Number of plugin update ticks in one budget window.")
      .default_val(1000);

    m_budget_per_window = param<uint64_t>("budget_per_window")
      .desc("Maximum number of TRR actions allowed in each budget window.")
      .default_val(32);

    parse_flip_mode(flip_mode_str);
    parse_prob_function(prob_function_str);
    validate_params();

    m_rng.seed(m_random_seed);

    m_act_count.assign(m_num_rows, 0);
    m_disturbance.assign(m_num_rows, 0);

    m_tick = 0;
    m_window_start_tick = 0;
    m_budget_used = 0;
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

    log_setup();
  }

  void update(bool request_found, ReqBuffer::iterator& req_it) override {
    advance_time_and_budget_window();

    if (!request_found || m_dram == nullptr) {
      return;
    }

    if (!is_row_opening_request(req_it)) {
      return;
    }

    s_num_openings++;

    const int aggressor_row = get_row(req_it);
    if (!is_valid_row(aggressor_row)) {
      return;
    }

    m_act_count[aggressor_row]++;

    if (m_act_count[aggressor_row] < m_hammer_threshold) {
      return;
    }

    // One effective hammer event
    s_num_hammer_events++;
    m_act_count[aggressor_row] = 0;

    log_hammer_event(aggressor_row);

    process_victim(aggressor_row - 1, aggressor_row);
    process_victim(aggressor_row + 1, aggressor_row);
  }

  void finalize() override {
    std::cout
      << "[UniformTRR][summary] "
      << "openings=" << s_num_openings
      << " hammer_events=" << s_num_hammer_events
      << " trr_events=" << s_num_trr_events
      << " bitflips=" << s_num_bitflips
      << " budget_blocks=" << s_num_budget_blocks
      << " budget_resets=" << s_num_budget_resets
      << " prob_checks=" << s_num_prob_checks
      << " prob_successes=" << s_num_prob_successes
      << " budget_used_final_window=" << m_budget_used
      << "/" << m_budget_per_window
      << std::endl;
  }

private:
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
    if (m_trr_threshold <= 0) {
      throw ConfigurationError("UniformTRR: trr_threshold must be positive.");
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
    if (m_budget_window == 0) {
      throw ConfigurationError("UniformTRR: budget_window must be positive.");
    }
  }

  bool is_valid_row(int row) const {
    return row >= 0 && row < m_num_rows;
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

  void advance_time_and_budget_window() {
    m_tick++;

    if (m_tick - m_window_start_tick >= m_budget_window) {
      if (m_debug && m_budget_used > 0) {
        std::cout
          << "[UniformTRR][budget-reset] "
          << "tick=" << m_tick
          << " used=" << m_budget_used << "/" << m_budget_per_window
          << std::endl;
      }

      m_window_start_tick = m_tick;
      m_budget_used = 0;
      s_num_budget_resets++;
    }
  }

  bool budget_available() const {
    return m_budget_used < m_budget_per_window;
  }

  void consume_budget() {
    m_budget_used++;
  }

  void process_victim(int victim_row, int aggressor_row) {
    if (!is_valid_row(victim_row)) {
      return;
    }

    m_disturbance[victim_row]++;

    if (m_enable_trr && m_disturbance[victim_row] >= m_trr_threshold) {
      if (budget_available()) {
        log_trr_fire(victim_row, aggressor_row);
        m_disturbance[victim_row] = 0;
        consume_budget();
        s_num_trr_events++;
        return;
      } else {
        s_num_budget_blocks++;
        log_trr_block(victim_row, aggressor_row);
      }
    }

    if (m_flip_mode == FlipMode::Deterministic) {
      maybe_bitflip_deterministic(victim_row, aggressor_row);
    } else {
      maybe_bitflip_probabilistic(victim_row, aggressor_row);
    }
  }

  void maybe_bitflip_deterministic(int victim_row, int aggressor_row) {
    if (m_disturbance[victim_row] >= m_flip_threshold) {
      log_bitflip(victim_row, aggressor_row, -1.0, -1.0);
      s_num_bitflips++;
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
      s_num_prob_successes++;
      s_num_bitflips++;
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
        const double p = m_prob_alpha * static_cast<double>(disturbance - m_prob_flip_start + 1);
        return clamp_probability(p);
      }

      case ProbFunction::Sigmoid: {
        const double x = static_cast<double>(disturbance) - m_prob_midpoint;
        const double p = 1.0 / (1.0 + std::exp(-m_prob_k * x));
        return clamp_probability(p);
      }

      case ProbFunction::Step: {
        return 1.0;
      }
    }

    return 0.0;
  }

  static double clamp_probability(double p) {
    if (p < 0.0) return 0.0;
    if (p > 1.0) return 1.0;
    return p;
  }

  void log_setup() const {
    if (!m_debug) {
      return;
    }

    std::cout
      << "[UniformTRR][setup] "
      << "row_level_idx=" << m_row_level_idx
      << " enable_trr=" << (m_enable_trr ? "true" : "false")
      << " hammer_threshold=" << m_hammer_threshold
      << " trr_threshold=" << m_trr_threshold
      << " flip_threshold=" << m_flip_threshold
      << " flip_mode=" << flip_mode_name(m_flip_mode)
      << " prob_function=" << prob_function_name(m_prob_function)
      << " prob_flip_start=" << m_prob_flip_start
      << " prob_alpha=" << m_prob_alpha
      << " prob_k=" << m_prob_k
      << " prob_midpoint=" << m_prob_midpoint
      << " random_seed=" << m_random_seed
      << " budget_window=" << m_budget_window
      << " budget_per_window=" << m_budget_per_window
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

  void log_trr_fire(int victim_row, int aggressor_row) const {
    if (!m_debug) {
      return;
    }

    std::cout
      << "[UniformTRR][trr] "
      << "victim=" << victim_row
      << " aggressor=" << aggressor_row
      << " disturbance=" << m_disturbance[victim_row]
      << " budget=" << m_budget_used << "/" << m_budget_per_window
      << std::endl;
  }

  void log_trr_block(int victim_row, int aggressor_row) const {
    if (!m_debug) {
      return;
    }

    std::cout
      << "[UniformTRR][blocked] "
      << "victim=" << victim_row
      << " aggressor=" << aggressor_row
      << " disturbance=" << m_disturbance[victim_row]
      << " budget=" << m_budget_used << "/" << m_budget_per_window
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
      << " disturbance=" << m_disturbance[victim_row];

    if (m_flip_mode == FlipMode::Probabilistic) {
      std::cout << " p=" << p << " sample=" << u;
    }

    std::cout << std::endl;
  }
};

} // namespace Ramulator

