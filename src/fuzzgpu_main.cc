#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"
#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>

#include "bug_filter.hh"
#include "isa/riscv/riscv_meta_list.hh"
#include "util.hh"

#include "generator.hh"

using namespace fuzzgpu;

int main(int argc, char **argv) {
  argparse::ArgumentParser program("fuzzgpu");

  program.add_argument("out-instr")
      .help("output instr binary file")
      .default_value(std::string(""));
  program.add_argument("out-data")
      .help("output data file")
      .default_value(std::string(""));
  program.add_argument("out-aux")
      .help("output aux file")
      .default_value(std::string(""));
  program.add_argument("out-meta-json")
      .help("output meta json file")
      .default_value(std::string(""));

  program.add_argument("--seed")
      .help("random seed")
      .default_value(47u)
      .scan<'u', unsigned>();

  program.add_argument("--trace")
      .help("output emulation trace files")
      .default_value("");

  program.add_argument("--gmem-size")
      .help("global memory size (in bytes)")
      .default_value(size_t(0x100000000ULL))
      .scan<'u', size_t>();
  program.add_argument("--num-genblocks")
      .help("number of generated blocks")
      .default_value(2u)
      .scan<'u', unsigned>();
  program.add_argument("--num-genwarps-per-block")
      .help("number of generated warps per block")
      .default_value(5u)
      .scan<'u', unsigned>();

  program.add_argument("--max-num-fuzzing-instrs-per-bb")
      .help("maximum number of fuzzing instructions per basic block")
      .default_value(size_t(500))
      .scan<'u', size_t>();

  program.add_argument("--min-num-fuzzing-instrs-per-bb")
      .help("minimum number of fuzzing instructions per basic block")
      .default_value(size_t(1))
      .scan<'u', size_t>();

  program.add_argument("--max-num-instrs")
      .help("maximum total number of instructions")
      .default_value(size_t(-1))
      .scan<'u', size_t>();

  program.add_argument("--min-tmask-flips")
      .help("minimum tmask flips")
      .default_value(1u)
      .scan<'u', unsigned>();

  program.add_argument("--init-as-float-prob")
      .help("initial-as-float probability")
      .default_value(0.5)
      .scan<'g', double>();

  program.add_argument("--force-lds-prob")
      .help("probability to force init load values into constrained range "
            "(Ventus only)")
      .default_value(0.2)
      .scan<'g', double>();

  program.add_argument("--branch-taken-prob")
      .help("branch taken probability")
      .default_value(0.5)
      .scan<'g', double>();

  program.add_argument("--branch-major-taken-prob")
      .help("branch major-taken probability")
      .default_value(0.5)
      .scan<'g', double>();

  program.add_argument("--branch-divergent-prob")
      .help("branch divergent probability")
      .default_value(0.8)
      .scan<'g', double>();

  program.add_argument("--sel-then-path-prob")
      .help("select then-path probability")
      .default_value(0.5)
      .scan<'g', double>();

  program.add_argument("--replay-load-store-operands-prob")
      .help("replay load/store operands probability")
      .default_value(0.8)
      .scan<'g', double>();

  program.add_argument("--instr-pool-node-size")
      .help("instruction pool node size")
      .default_value(size_t(10000))
      .scan<'u', size_t>();

  program.add_argument("--max-alloc-tries")
      .help("maximum allocation tries")
      .default_value(size_t(1000))
      .scan<'u', size_t>();

  program.add_argument("--max-resample-times")
      .help("maximum resample times")
      .default_value(10u)
      .scan<'u', unsigned>();

  program.add_argument("--get-registered-instr-count")
      .help("get the count of registered instructions")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--get-registered-instr-name")
      .help("get the name of all registered instructions")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--get-registered-instr-info")
      .help("get instruction info (name, category, group) as JSON")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--instr-prob-json")
      .help("instruction probability JSON file")
      .default_value(std::string(""));

  program.add_argument("--bug-filter-json")
      .help("bug filter list JSON file (contains list of bugs to filter)")
      .default_value(std::string(""));

  program.add_argument("--log-level")
      .help("set log level (trace, debug, info, warn, err, critical, off)")
      .default_value(std::string("info"));

  program.add_argument("--disable-bug-filter")
      .help("disable bug filtering even if a bug filter JSON file is provided")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--disable-default-bug-filtering")
      .help("disable default bug filtering (only use provided bug filter JSON)")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--disable-emu-guide")
      .help("disable emulation guided generation")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--disable-barrier-plan")
      .help("disable barrier planning during generation")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--disable-taint")
      .help("disable taint analysis during emulation")
      .default_value(false)
      .implicit_value(true);

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
    return 1;
  }

  // Validate required output arguments when not just querying instruction count
  // or name or info
  if (!program.get<bool>("--get-registered-instr-count") &&
      !program.get<bool>("--get-registered-instr-name") &&
      !program.get<bool>("--get-registered-instr-info")) {
    const auto out_instr = program.get<std::string>("out-instr");
    const auto out_data = program.get<std::string>("out-data");
    const auto out_aux = program.get<std::string>("out-aux");
    const auto out_meta_json = program.get<std::string>("out-meta-json");

    if (out_instr.empty() || out_data.empty() || out_aux.empty() ||
        out_meta_json.empty()) {
      std::cerr << "Error: out-instr, out-data, out-aux, and out-meta-json are "
                   "required when --get-registered-instr-count is not set"
                << std::endl;
      std::cerr << program;
      return 1;
    }
  }

  // configure log level early so downstream logging uses the setting
  const auto log_level_str = program.get<std::string>("--log-level");
  const auto to_lower = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return s;
  };

  const auto normalized_level = to_lower(log_level_str);
  spdlog::level::level_enum level = spdlog::level::info;
  if (normalized_level == "trace") {
    level = spdlog::level::trace;
  } else if (normalized_level == "debug") {
    level = spdlog::level::debug;
  } else if (normalized_level == "info") {
    level = spdlog::level::info;
  } else if (normalized_level == "warn" || normalized_level == "warning") {
    level = spdlog::level::warn;
  } else if (normalized_level == "err" || normalized_level == "error") {
    level = spdlog::level::err;
  } else if (normalized_level == "critical") {
    level = spdlog::level::critical;
  } else if (normalized_level == "off") {
    level = spdlog::level::off;
  } else {
    std::cerr << "Error: invalid log level '" << log_level_str << "'"
              << std::endl;
    std::cerr << program;
    return 1;
  }

  spdlog::set_level(level);
  spdlog::set_pattern("[%^%l%$] %v");

  // load the ISA emulator
  difftest::ISAEmulator tmp_isa_emu; // used to get arch info only
  Config::create(tmp_isa_emu.get_arch(), program.get<size_t>("--gmem-size"),
                 program.get<unsigned>("--num-genblocks"),
                 program.get<unsigned>("--num-genwarps-per-block"));
  // set config from parsed arguments
  Config::instance().max_num_fuzzing_instrs_per_bb =
      program.get<size_t>("--max-num-fuzzing-instrs-per-bb");
  Config::instance().min_num_fuzzing_instrs_per_bb =
      program.get<size_t>("--min-num-fuzzing-instrs-per-bb");

  Config::instance().max_num_instrs = program.get<size_t>("--max-num-instrs");

  Config::instance().min_tmask_flips =
      program.get<unsigned>("--min-tmask-flips");

  Config::instance().force_lds_prob = program.get<double>("--force-lds-prob");

  Config::instance().branch_taken_prob =
      program.get<double>("--branch-taken-prob");
  Config::instance().branch_major_taken_prob =
      program.get<double>("--branch-major-taken-prob");
  Config::instance().branch_divergent_prob =
      program.get<double>("--branch-divergent-prob");

  Config::instance().replay_load_store_operands_prob =
      program.get<double>("--replay-load-store-operands-prob");

  Config::instance().instr_pool_node_size =
      program.get<size_t>("--instr-pool-node-size");
  Config::instance().max_alloc_tries = program.get<size_t>("--max-alloc-tries");

  Config::instance().max_resample_times =
      program.get<unsigned>("--max-resample-times");

  if (program.get<bool>("--disable-emu-guide")) {
    Config::instance().emu_guide = false;
  }
  if (program.get<bool>("--disable-barrier-plan")) {
    Config::instance().barrier_plan = false;
  }
  if (program.get<bool>("--disable-taint")) {
    Config::instance().taint = false;
  }

  // Load bug filter list from JSON file if provided
  nlohmann::json bug_filter_list = nlohmann::json::array();
  const auto bug_filter_json_file =
      program.get<std::string>("--bug-filter-json");
  if (!bug_filter_json_file.empty()) {
    try {
      std::ifstream bug_filter_stream(bug_filter_json_file);
      if (bug_filter_stream) {
        nlohmann::json bug_filter_json;
        bug_filter_stream >> bug_filter_json;
        bug_filter_stream.close();

        // Extract the filtered_bugs array from the JSON
        if (bug_filter_json.is_object() &&
            bug_filter_json.contains("filtered_bugs")) {
          bug_filter_list = bug_filter_json["filtered_bugs"];
        } else if (bug_filter_json.is_array()) {
          bug_filter_list = bug_filter_json;
        }
        spdlog::info("Loaded {} bugs from filter list", bug_filter_list.size());
      } else {
        spdlog::warn("Failed to open bug filter JSON file: {}",
                     bug_filter_json_file);
      }
    } catch (const std::exception &e) {
      spdlog::error("Error reading bug filter JSON: {}", e.what());
    }
  }

  // Register bugs with optional filter list from JSON
  if (program.get<bool>("--disable-bug-filter")) {
    BugFilter::instance().disable();
  }
  if (program.get<bool>("--disable-default-bug-filtering")) {
    BugFilter::instance().disable_default_filtering();
  }
  BugFilter::instance().register_bugs(bug_filter_list);

  Config::instance().dump();
  riscv_meta_list::create(Config::instance().arch);

  auto state = std::make_unique<FuzzerState>(program.get<unsigned>("--seed"));

  if (program.get<bool>("--get-registered-instr-count")) {
    std::cout << "registered-instr-count = "
              << state->get_registered_instr_count() << std::endl;
    return 0;
  }

  if (program.get<bool>("--get-registered-instr-name")) {
    const auto &instr_names = state->get_registered_instr_names();
    const std::string instr_name_prefix = "INSTR_NAME: ";
    for (const auto &instr_name : instr_names) {
      std::string instr_name_str =
          std::string(magic_enum::enum_name(instr_name));
      std::transform(
          instr_name_str.begin(), instr_name_str.end(), instr_name_str.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      std::replace(instr_name_str.begin(), instr_name_str.end(), '_', '.');
      std::cout << instr_name_prefix << instr_name_str << std::endl;
    }
    return 0;
  }

  if (program.get<bool>("--get-registered-instr-info")) {
    const auto &instr_names = state->get_registered_instr_names();
    nlohmann::json instr_info_json = nlohmann::json::array();
    for (const auto &instr_name : instr_names) {
      const auto &meta = riscv_meta_list::instance().instr(instr_name);
      std::string instr_name_str =
          std::string(magic_enum::enum_name(instr_name));
      std::transform(
          instr_name_str.begin(), instr_name_str.end(), instr_name_str.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      std::replace(instr_name_str.begin(), instr_name_str.end(), '_', '.');

      std::string category_str =
          std::string(magic_enum::enum_name(meta->category));
      std::transform(
          category_str.begin(), category_str.end(), category_str.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

      std::string group_str = std::string(magic_enum::enum_name(meta->group));
      std::transform(
          group_str.begin(), group_str.end(), group_str.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

      instr_info_json.push_back({{"name", instr_name_str},
                                 {"category", category_str},
                                 {"group", group_str}});
    }
    // Use special markers for reliable extraction by scripts
    std::cout << "INSTR_INFO_JSON_BEGIN" << std::endl;
    std::cout << instr_info_json.dump(2) << std::endl;
    std::cout << "INSTR_INFO_JSON_END" << std::endl;
    return 0;
  }

  const auto instr_prob_json_file =
      program.get<std::string>("--instr-prob-json");
  if (!instr_prob_json_file.empty()) {
    spdlog::info("Loading instruction probability from JSON file {}",
                 instr_prob_json_file);
    std::ifstream instr_prob_json_stream(instr_prob_json_file);
    if (!instr_prob_json_stream) {
      spdlog::error("Failed to open instruction probability JSON file {}",
                    instr_prob_json_file);
      return 1;
    }
    nlohmann::json instr_prob_json;
    instr_prob_json_stream >> instr_prob_json;
    instr_prob_json_stream.close();
    const auto probs = instr_prob_json["prob"].get<std::vector<double>>();
    if (probs.size() != state->get_registered_instr_count()) {
      spdlog::error(
          "Instruction probability size mismatch: expected {}, got {}",
          state->get_registered_instr_count(), probs.size());
      return 1;
    }
    state->set_instr_prob(probs);
  }

  auto riscv_isa = std::make_unique<Generator>(*state);

  const auto trace_file_prefix = program.get<std::string>("--trace");
  if (!trace_file_prefix.empty()) {
    spdlog::info("Outputting emulation traces with prefix {}",
                 trace_file_prefix);
  }
  const auto instr_binfile = program.get<std::string>("out-instr");
  const auto data_binfile = program.get<std::string>("out-data");
  const auto aux_binfile = program.get<std::string>("out-aux");
  std::ofstream instr_stream(instr_binfile, std::ios::binary);
  std::ofstream data_stream(data_binfile, std::ios::binary);
  std::ofstream aux_stream(aux_binfile, std::ios::binary);
  if (!instr_stream) {
    spdlog::error("Failed to open output file {}", instr_binfile);
    return 1;
  }
  if (!data_stream) {
    spdlog::error("Failed to open output file {}", data_binfile);
    return 1;
  }
  if (!aux_stream) {
    spdlog::error("Failed to open output file {}", aux_binfile);
    return 1;
  }
  instr_stream.exceptions(std::ios::failbit | std::ios::badbit);
  data_stream.exceptions(std::ios::failbit | std::ios::badbit);
  aux_stream.exceptions(std::ios::failbit | std::ios::badbit);

  spdlog::info("Generating binary to {} {}", instr_binfile, data_binfile);
  nlohmann::json meta_json;
  riscv_isa->generate_backbone(instr_stream, data_stream, aux_stream, meta_json,
                               trace_file_prefix);
  instr_stream.close();
  data_stream.close();
  aux_stream.close();

  const auto meta_json_file = program.get<std::string>("out-meta-json");
  std::ofstream meta_json_stream(meta_json_file);
  if (!meta_json_stream) {
    spdlog::error("Failed to open output file {}", meta_json_file);
    return 1;
  }
  meta_json_stream << meta_json.dump(2);
  meta_json_stream.close();

  return 0;
}
