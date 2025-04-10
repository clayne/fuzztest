// Copyright 2023 The Centipede Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "./centipede/environment_flags.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>  // NOLINT
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "./centipede/environment.h"
#include "./common/logging.h"

using ::fuzztest::internal::Environment;

// TODO(kcc): document usage of standalone binaries and how to use @@ wildcard.
// If the "binary" contains @@, it means the binary can only accept inputs
// from the command line, and only one input per process.
// @@ will be replaced with a path to file with the input.
// @@ is chosen to follow the AFL command line syntax.
// TODO(kcc): rename --binary to --command (same for --extra_binaries),
// while remaining backward compatible.
ABSL_FLAG(std::string, binary, Environment::Default().binary,
          "The target binary.");
ABSL_FLAG(std::string, coverage_binary, Environment::Default().coverage_binary,
          "The actual binary from which coverage is collected - if different "
          "from --binary.");
ABSL_FLAG(std::string, binary_hash, Environment::Default().binary_hash,
          "If not-empty, this hash string is used instead of the hash of the "
          "contents of coverage_binary. Use this flag when the coverage_binary "
          "is not available nor needed, e.g. when using --distill.");
ABSL_FLAG(std::string, clang_coverage_binary,
          Environment::Default().clang_coverage_binary,
          "A clang source-based code coverage binary used to produce "
          "human-readable reports. Do not add this binary to extra_binaries. "
          "You must have llvm-cov and llvm-profdata in your path to generate "
          "the reports. --workdir in turn must be local in order for this "
          "functionality to work. See "
          "https://clang.llvm.org/docs/SourceBasedCodeCoverage.html");
ABSL_FLAG(std::vector<std::string>, extra_binaries,
          Environment::Default().extra_binaries,
          "A comma-separated list of extra target binaries. These binaries are "
          "fed the same inputs as the main binary, but the coverage feedback "
          "from them is not collected. Use this e.g. to run the target under "
          "sanitizers.");
ABSL_FLAG(std::string, workdir, Environment::Default().workdir,
          "The working directory.");
ABSL_FLAG(std::string, merge_from, Environment::Default().merge_from,
          "Another working directory to merge the corpus from. Inputs from "
          "--merge_from will be added to --workdir if the add new features.");
ABSL_FLAG(size_t, num_runs, Environment::Default().num_runs,
          "Number of inputs to run per shard (see --total_shards).");
ABSL_FLAG(size_t, seed, Environment::Default().seed,
          "A seed for the random number generator. If 0, some other random "
          "number is used as seed.");
ABSL_FLAG(size_t, total_shards, Environment::Default().total_shards,
          "Number of shards.");
ABSL_FLAG(size_t, first_shard_index, Environment::Default().my_shard_index,
          "Index of the first shard, [0, --total_shards - --num_threads].");
ABSL_FLAG(size_t, num_threads, Environment::Default().num_threads,
          "Number of threads to execute in one process. i-th thread, where i "
          "is in [0, --num_threads), will work on shard "
          "(--first_shard_index + i).");
ABSL_FLAG(size_t, j, Environment::Default().j,
          "If not 0, --j=N is a shorthand for "
          "--num_threads=N --total_shards=N --first_shard_index=0. "
          "Overrides values of these flags if they are also used.");
ABSL_FLAG(size_t, max_len, Environment::Default().max_len,
          "Max length of mutants. Passed to mutator.");
ABSL_FLAG(size_t, batch_size, Environment::Default().batch_size,
          "The number of inputs given to the target at one time. Batches of "
          "more than 1 input are used to amortize the process start-up cost.")
    .OnUpdate([]() {
      QCHECK_GT(absl::GetFlag(FLAGS_batch_size), 0)
          << "--" << FLAGS_batch_size.Name() << " must be non-zero";
    });
ABSL_FLAG(size_t, mutate_batch_size, Environment::Default().mutate_batch_size,
          "Mutate this many inputs to produce batch_size mutants");
ABSL_FLAG(bool, use_legacy_default_mutator,
          Environment::Default().use_legacy_default_mutator,
          "When set, use the legacy ByteArrayMutator as the default mutator. "
          "Otherwise, the FuzzTest domain based mutator will be used.");
ABSL_FLAG(size_t, load_other_shard_frequency,
          Environment::Default().load_other_shard_frequency,
          "Load a random other shard after processing this many batches. Use 0 "
          "to disable loading other shards.  For now, choose the value of this "
          "flag so that shard loads happen at most once in a few minutes. In "
          "future we may be able to find the suitable value automatically.");
// TODO(b/262798184): Remove once the bug is fixed.
ABSL_FLAG(bool, serialize_shard_loads,
          Environment::Default().serialize_shard_loads,
          "When this flag is on, shard loading is serialized. "
          " Useful to avoid excessive RAM consumption when loading more"
          " that one shard at a time. Currently, loading a single large shard"
          " may create too many temporary heap allocations. "
          " This means, if we load many large shards concurrently,"
          " we may run out or RAM.");
ABSL_FLAG(size_t, prune_frequency, Environment::Default().prune_frequency,
          "Prune the corpus every time after this many inputs were added. If "
          "zero, pruning is disabled. Pruning removes redundant inputs from "
          "the corpus, e.g. inputs that have only \"frequent\", i.e. "
          "uninteresting features. When the corpus gets larger than "
          "--max_corpus_size, some random elements may also be removed.");
ABSL_FLAG(size_t, address_space_limit_mb,
          Environment::Default().address_space_limit_mb,
          "If not zero, instructs the target to set setrlimit(RLIMIT_AS) to "
          "this number of megabytes. Some targets (e.g. if built with ASAN, "
          "which can't run with RLIMIT_AS) may choose to ignore this flag. See "
          "also --rss_limit_mb.");
ABSL_FLAG(size_t, rss_limit_mb, Environment::Default().rss_limit_mb,
          "If not zero, instructs the target to fail if RSS goes over this "
          "number of megabytes and report an OOM. See also "
          "--address_space_limit_mb. These two flags have somewhat different "
          "meaning. --address_space_limit_mb does not allow the process to "
          "grow the used address space beyond the limit. --rss_limit_mb runs a "
          "background thread that monitors max RSS and also checks max RSS "
          "after executing every input, so it may detect OOM late. However "
          "--rss_limit_mb allows Centipede to *report* an OOM condition in "
          "most cases, while --address_space_limit_mb will cause a crash that "
          "may be hard to attribute to OOM.");
ABSL_FLAG(size_t, stack_limit_kb, Environment::Default().stack_limit_kb,
          "If not zero, instructs the target to fail if stack usage goes over "
          "this number of KiB.");
ABSL_FLAG(size_t, timeout_per_input, Environment::Default().timeout_per_input,
          "If not zero, the timeout in seconds for a single input. If an input "
          "runs longer than this, the runner process will abort. Support may "
          "vary depending on the runner.");
ABSL_FLAG(size_t, timeout, Environment::Default().timeout_per_input,
          "An alias for --timeout_per_input. If both are passed, the last of "
          "the two wins.")
    .OnUpdate([]() {
      absl::SetFlag(&FLAGS_timeout_per_input, absl::GetFlag(FLAGS_timeout));
    });
ABSL_FLAG(size_t, timeout_per_batch, Environment::Default().timeout_per_batch,
          "If not zero, the collective timeout budget in seconds for a single "
          "batch of inputs. Each input in a batch still has up to "
          "--timeout_per_input seconds to finish, but the entire batch must "
          "finish within --timeout_per_batch seconds. The default is computed "
          "as a function of --timeout_per_input * --batch_size. Support may "
          "vary depending on the runner.");
ABSL_FLAG(bool, ignore_timeout_reports,
          Environment::Default().ignore_timeout_reports,
          "If set, will ignore reporting timeouts as errors.");
ABSL_FLAG(absl::Time, stop_at, Environment::Default().stop_at,
          "Stop fuzzing in all shards (--total_shards) at approximately this "
          "time in ISO-8601/RFC-3339 format, e.g. 2023-04-06T23:35:02Z. "
          "If a given shard is still running at that time, it will gracefully "
          "wind down by letting the current batch of inputs to finish and then "
          "exiting. A special value 'infinite-future' (the default) is "
          "supported. Tip: `date` is useful for conversion of mostly free "
          "format human readable date/time strings, e.g. "
          "--stop_at=$(date --date='next Monday 6pm' --utc --iso-8601=seconds) "
          ". Also see --stop_after. These two flags are mutually exclusive.");
ABSL_FLAG(absl::Duration, stop_after, absl::InfiniteDuration(),
          "Equivalent to setting --stop_at to the current date/time + this "
          "duration. These two flags are mutually exclusive.");
ABSL_FLAG(bool, fork_server, Environment::Default().fork_server,
          "If true (default) tries to execute the target(s) via the fork "
          "server, if supported by the target(s). Prepend the binary path with "
          "'%f' to disable the fork server. --fork_server applies to binaries "
          "passed via these flags: --binary, --extra_binaries, "
          "--input_filter.");
ABSL_FLAG(bool, full_sync, Environment::Default().full_sync,
          "Perform a full corpus sync on startup. If true, feature sets and "
          "corpora are read from all shards before fuzzing. This way fuzzing "
          "starts with a full knowledge of the current state and will avoid "
          "adding duplicating inputs. This however is very expensive when the "
          "number of shards is very large.");
ABSL_FLAG(bool, use_corpus_weights, Environment::Default().use_corpus_weights,
          "If true, use weighted distribution when choosing the corpus element "
          "to mutate. This flag is mostly for Centipede developers.");
ABSL_FLAG(bool, use_coverage_frontier,
          Environment::Default().use_coverage_frontier,
          "If true, use coverage frontier when choosing the corpus element to "
          "mutate. This flag is mostly for Centipede developers.");
ABSL_FLAG(size_t, max_corpus_size, Environment::Default().max_corpus_size,
          "Indicates the number of inputs in the in-memory corpus after which"
          "more aggressive pruning will be applied.");
ABSL_FLAG(size_t, crossover_level, Environment::Default().crossover_level,
          "Defines how much crossover is used during mutations. 0 means no "
          "crossover, 100 means the most aggressive crossover. See "
          "https://en.wikipedia.org/wiki/Crossover_(genetic_algorithm).");
ABSL_FLAG(bool, use_pc_features, Environment::Default().use_pc_features,
          "When available from instrumentation, use features derived from "
          "PCs.");
ABSL_FLAG(bool, use_cmp_features, Environment::Default().use_cmp_features,
          "When available from instrumentation, use features derived from "
          "instrumentation of CMP instructions.");
ABSL_FLAG(size_t, callstack_level, Environment::Default().callstack_level,
          "When available from instrumentation, use features derived from "
          "observing the function call stacks. 0 means no callstack features."
          "Values between 1 and 100 define how aggressively to use the "
          "callstacks. Level N roughly corresponds to N call frames.")
    .OnUpdate([]() {
      QCHECK_LE(absl::GetFlag(FLAGS_callstack_level), 100)
          << "--" << FLAGS_callstack_level.Name() << " must be in [0,100]";
    });
ABSL_FLAG(bool, use_auto_dictionary, Environment::Default().use_auto_dictionary,
          "If true, use automatically-generated dictionary derived from "
          "intercepting comparison instructions, memcmp, and similar.");
ABSL_FLAG(size_t, path_level, 0,  // Not ready for wide usage.
          "When available from instrumentation, use features derived from "
          "bounded execution paths. Be careful, may cause exponential feature "
          "explosion. 0 means no path features. Values between 1 and 100 "
          "define how aggressively to use the paths.")
    .OnUpdate([]() {
      QCHECK_LE(absl::GetFlag(FLAGS_path_level), 100)
          << "--" << FLAGS_path_level.Name() << " must be in [0,100]";
    });
ABSL_FLAG(bool, use_dataflow_features,
          Environment::Default().use_dataflow_features,
          "When available from instrumentation, use features derived from "
          "data flows.");
ABSL_FLAG(bool, use_counter_features,
          Environment::Default().use_counter_features,
          "When available from instrumentation, use features derived from "
          "counting the number of occurrences of a given PC. When enabled, "
          "supersedes --use_pc_features.");
ABSL_FLAG(bool, use_pcpair_features, Environment::Default().use_pcpair_features,
          "If true, PC pairs are used as additional synthetic features. "
          "Experimental, use with care - it may explode the corpus.");
ABSL_FLAG(uint64_t, user_feature_domain_mask,
          Environment::Default().user_feature_domain_mask,
          "A bitmask indicating which user feature domains should be enabled. "
          "A value of zero will disable all user features.");
ABSL_FLAG(size_t, feature_frequency_threshold,
          Environment::Default().feature_frequency_threshold,
          "Internal flag. When a given feature is present in the corpus this "
          "many times Centipede will stop recording it for future corpus "
          "elements. Larger values will use more RAM but may improve corpus "
          "weights. Valid values are 2 - 255.")
    .OnUpdate([]() {
      size_t threshold = absl::GetFlag(FLAGS_feature_frequency_threshold);
      QCHECK(threshold >= 2 && threshold <= 255)
          << "--" << FLAGS_feature_frequency_threshold.Name()
          << " must be in [2,255] but has value " << threshold;
    });
ABSL_FLAG(bool, require_pc_table, Environment::Default().require_pc_table,
          "If true, Centipede will exit if the --pc_table is not found.");
ABSL_FLAG(bool, require_seeds, Environment::Default().require_seeds,
          "If true, Centipede will exit if no seed inputs are found.");
ABSL_FLAG(int, telemetry_frequency, Environment::Default().telemetry_frequency,
          "Dumping frequency for intermediate telemetry files, i.e. coverage "
          "report (workdir/coverage-report-BINARY.*.txt), corpus stats "
          "(workdir/corpus-stats-*.json), etc. Positive value N means dump "
          "every N batches. Negative N means start dumping after 2^N processed "
          "batches with exponential 2x back-off (e.g. for "
          "--telemetry_frequency=-5, dump on batches 32, 64, 128,...). Zero "
          "means no telemetry. Note that the before-fuzzing and after-fuzzing "
          "telemetry are always dumped.");
ABSL_FLAG(bool, print_runner_log, Environment::Default().print_runner_log,
          "If true, runner logs are printed after every batch. Note that "
          "crash logs are always printed regardless of this flag's value.");
ABSL_FLAG(std::string, knobs_file, Environment::Default().knobs_file,
          "If not empty, knobs will be read from this (possibly remote) file."
          " The feature is experimental, not yet fully functional.");
ABSL_FLAG(std::string, corpus_to_files, Environment::Default().corpus_to_files,
          "Save the remote corpus from working to the given directory, one "
          "file per corpus.");
ABSL_FLAG(std::string, corpus_from_files,
          Environment::Default().corpus_from_files,
          "Export a corpus from a local directory with one file per input into "
          "the sharded remote corpus in workdir. Not recursive.");
ABSL_FLAG(std::vector<std::string>, corpus_dir,
          Environment::Default().corpus_dir,
          "Comma-separated list of paths to local corpus dirs, with one file "
          "per input. At startup, the files are exported into the corpus in "
          "--workdir. While fuzzing, the new corpus elements are written to "
          "the first dir if it is not empty. This makes it more convenient to "
          "interop with libFuzzer corpora.");
ABSL_FLAG(std::string, symbolizer_path, Environment::Default().symbolizer_path,
          "Path to the symbolizer tool. By default, we use llvm-symbolizer "
          "and assume it is in PATH.");
ABSL_FLAG(std::string, objdump_path, Environment::Default().objdump_path,
          "Path to the objdump tool. By default, we use the system objdump "
          "and assume it is in PATH.");
ABSL_FLAG(std::string, runner_dl_path_suffix,
          Environment::Default().runner_dl_path_suffix,
          "If non-empty, this flag is passed to the Centipede runner. "
          "It tells the runner that this dynamic library is instrumented "
          "while the main binary is not. "
          "The value could be the full path, like '/path/to/my.so' "
          "or a suffix, like '/my.so' or 'my.so'."
          "This flag is experimental and may be removed in future");
// TODO(kcc): --distill and several others had better be dedicated binaries.
ABSL_FLAG(bool, distill, Environment::Default().distill,
          "Distill (minimize) the --total_shards input shards from --workdir "
          "into --num_threads output shards. The input shards are randomly and "
          "evenly divided between --num_threads concurrent distillation "
          "threads to speed up processing. The threads share and update the "
          "global coverage info as they go, so the output shards will never "
          "have identical input/feature pairs (some intputs can still be "
          "identical if a non-deterministic target produced different features "
          "for identical inputs in the corpus). The features.* files are "
          "looked up in a --workdir subdirectory that corresponds to "
          "--coverage_binary and --binary_hash, if --binary_hash is provided; "
          "if it is not provided, the actual hash of the --coverage_binary "
          "file on disk is computed and used. Therefore, with an explicit "
          "--binary_hash, --coverage_binary can be just the basename of the "
          "actual target binary; without it, it must be the full path. "
          "Each distillation thread writes a distilled corpus shard to "
          "to <--workdir>/distilled-<--coverage_binary basename>.<index>.");
ABSL_RETIRED_FLAG(size_t, distill_shards, 0,
                  "No longer supported: use --distill instead.");
ABSL_FLAG(size_t, log_features_shards,
          Environment::Default().log_features_shards,
          "The first --log_features_shards shards will log newly observed "
          "features as symbols. In most cases you don't need this to be >= 2.");
ABSL_FLAG(bool, exit_on_crash, Environment::Default().exit_on_crash,
          "If true, Centipede will exit on the first crash of the target.");
ABSL_FLAG(size_t, num_crash_reports,
          Environment::Default().max_num_crash_reports,
          "report this many crashes per shard.");
ABSL_FLAG(std::string, minimize_crash,
          Environment::Default().minimize_crash_file_path,
          "If non-empty, a path to an input file that triggers a crash."
          " Centipede will run the minimization loop and store smaller crashing"
          " inputs in workdir/crashes.NNNNNN/, where NNNNNN is "
          "--first_shard_index padded on the left with zeros. "
          " --num_runs and --num_threads apply. "
          " Assumes local workdir.");
ABSL_FLAG(bool, batch_triage_suspect_only,
          Environment::Default().batch_triage_suspect_only,
          "If set, triage the crash on only the suspected input in a crashing "
          "batch. Otherwise, triage on all the executed inputs");
ABSL_FLAG(std::string, input_filter, Environment::Default().input_filter,
          "Path to a tool that filters bad inputs. The tool is invoked as "
          "`input_filter INPUT_FILE` and should return 0 if the input is good "
          "and non-0 otherwise. Ignored if empty. The --input_filter is "
          "invoked only for inputs that are considered for addition to the "
          "corpus.");
ABSL_FLAG(std::string, for_each_blob, Environment::Default().for_each_blob,
          "If non-empty, extracts individual blobs from the files given as "
          "arguments, copies each blob to a temporary file, and applies this "
          "command to that temporary file. %P is replaced with the temporary "
          "file's path and %H is replaced with the blob's hash. Example:\n"
          "$ centipede --for_each_blob='ls -l  %P && echo %H' corpus.000000");
ABSL_FLAG(std::string, experiment, Environment::Default().experiment,
          "A colon-separated list of values, each of which is a flag followed "
          "by = and a comma-separated list of values. Example: "
          "'foo=1,2,3:bar=10,20'. When non-empty, this flag is used to run an "
          "A/B[/C/D...] experiment: different threads will set different "
          "values of 'foo' and 'bar' and will run independent fuzzing "
          "sessions. If more than one flag is given, all flag combinations are "
          "tested. In example above: '--foo=1 --bar=10' ... "
          "'--foo=3 --bar=20'. The number of threads should be multiple of the "
          "number of flag combinations.");
ABSL_FLAG(bool, analyze, Environment::Default().analyze,
          "If set, Centipede will read the corpora from the work dirs provided"
          " as argv. If two corpora are provided, then analyze differences"
          " between those corpora. If one corpus is provided, then save the"
          " coverage report to a file within workdir with prefix"
          " 'coverage-report-'.");
ABSL_FLAG(std::vector<std::string>, dictionary,
          Environment::Default().dictionary,
          "A comma-separated list of paths to dictionary files. The dictionary "
          "file is either in AFL/libFuzzer plain text format or in the binary "
          "Centipede corpus file format. The flag is interpreted by "
          "CentipedeCallbacks so its meaning may be different in custom "
          "implementations of CentipedeCallbacks.");
ABSL_FLAG(std::string, function_filter, Environment::Default().function_filter,
          "A comma-separated list of functions that fuzzing needs to focus on. "
          "If this list is non-empty, the fuzzer will mutate only those inputs "
          "that trigger code in one of these functions.");
ABSL_FLAG(size_t, shmem_size_mb, Environment::Default().shmem_size_mb,
          "Size of the shared memory regions used to communicate between the "
          "ending and the runner.");
ABSL_FLAG(bool, use_posix_shmem, Environment::Default().use_posix_shmem,
          "[INTERNAL] When true, uses shm_open/shm_unlink instead of "
          "memfd_create to allocate shared memory. You may want this if your "
          "target doesn't have access to /proc/<arbitrary_pid> subdirs or the "
          "memfd_create syscall is not supported.");
ABSL_FLAG(bool, dry_run, Environment::Default().dry_run,
          "Initializes as much of Centipede as possible without actually "
          "running any fuzzing. Useful to validate the rest of the command "
          "line, verify existence of all the input directories and files, "
          "etc. Also useful in combination with --save_config or "
          "--update_config to stop execution immediately after writing the "
          "(updated) config file.");
ABSL_FLAG(bool, save_binary_info, Environment::Default().save_binary_info,
          "Save the BinaryInfo from the fuzzing run within the working "
          "directory.");
ABSL_FLAG(bool, populate_binary_info,
          Environment::Default().populate_binary_info,
          "Get binary info from a coverage instrumented binary. This should "
          "only be turned off when coverage is not based on instrumenting some "
          "binary.");
#ifndef CENTIPEDE_DISABLE_RIEGELI
ABSL_FLAG(bool, riegeli, Environment::Default().riegeli,
          "Use Riegeli file format (instead of the legacy bespoke encoding) "
          "for storage");
#endif  // CENTIPEDE_DISABLE_RIEGELI

namespace fuzztest::internal {

namespace {

// Computes the final stop-at time based on the possibly user-provided inputs.
absl::Time GetStopAtTime(absl::Time stop_at, absl::Duration stop_after) {
  const bool stop_at_is_non_default = stop_at != absl::InfiniteFuture();
  const bool stop_after_is_non_default = stop_after != absl::InfiniteDuration();
  CHECK_LE(stop_at_is_non_default + stop_after_is_non_default, 1)
      << "At most one of --stop_at and --stop_after should be specified, "
         "including via --config file: "
      << VV(stop_at) << VV(stop_after);
  if (stop_at_is_non_default) {
    return stop_at;
  } else if (stop_after_is_non_default) {
    return absl::Now() + stop_after;
  } else {
    return absl::InfiniteFuture();
  }
}

}  // namespace

Environment CreateEnvironmentFromFlags(const std::vector<std::string> &argv) {
  const auto binary = absl::GetFlag(FLAGS_binary);
  const auto coverage_binary =
      absl::GetFlag(FLAGS_coverage_binary).empty()
          ? std::string(binary.empty() ? ""
                                       : *absl::StrSplit(binary, ' ').begin())
          : absl::GetFlag(FLAGS_coverage_binary);
  Environment env_from_flags = {
      /*binary=*/binary,
      /*coverage_binary=*/coverage_binary,
      /*clang_coverage_binary=*/absl::GetFlag(FLAGS_clang_coverage_binary),
      /*extra_binaries=*/absl::GetFlag(FLAGS_extra_binaries),
      /*workdir=*/absl::GetFlag(FLAGS_workdir),
      /*merge_from=*/absl::GetFlag(FLAGS_merge_from),
      /*num_runs=*/absl::GetFlag(FLAGS_num_runs),
      /*total_shards=*/absl::GetFlag(FLAGS_total_shards),
      /*my_shard_index=*/absl::GetFlag(FLAGS_first_shard_index),
      /*num_threads=*/absl::GetFlag(FLAGS_num_threads),
      /*j=*/Environment::Default().j,
      /*max_len*/ absl::GetFlag(FLAGS_max_len),
      /*batch_size=*/absl::GetFlag(FLAGS_batch_size),
      /*mutate_batch_size=*/absl::GetFlag(FLAGS_mutate_batch_size),
      /*use_legacy_default_mutator=*/
      absl::GetFlag(FLAGS_use_legacy_default_mutator),
      /*load_other_shard_frequency=*/
      absl::GetFlag(FLAGS_load_other_shard_frequency),
      /*serialize_shard_loads=*/absl::GetFlag(FLAGS_serialize_shard_loads),
      /*seed=*/absl::GetFlag(FLAGS_seed),
      /*prune_frequency=*/absl::GetFlag(FLAGS_prune_frequency),
      /*address_space_limit_mb=*/absl::GetFlag(FLAGS_address_space_limit_mb),
      /*rss_limit_mb=*/absl::GetFlag(FLAGS_rss_limit_mb),
      /*stack_limit_kb=*/absl::GetFlag(FLAGS_stack_limit_kb),
      /*timeout_per_input=*/absl::GetFlag(FLAGS_timeout_per_input),
      /*timeout_per_batch=*/absl::GetFlag(FLAGS_timeout_per_batch),
      /*ignore_timeout_reports=*/absl::GetFlag(FLAGS_ignore_timeout_reports),
      /*stop_at=*/
      GetStopAtTime(absl::GetFlag(FLAGS_stop_at),
                    absl::GetFlag(FLAGS_stop_after)),
      /*fork_server=*/absl::GetFlag(FLAGS_fork_server),
      /*full_sync=*/absl::GetFlag(FLAGS_full_sync),
      /*use_corpus_weights=*/absl::GetFlag(FLAGS_use_corpus_weights),
      /*use_coverage_frontier=*/absl::GetFlag(FLAGS_use_coverage_frontier),
      /*max_corpus_size=*/absl::GetFlag(FLAGS_max_corpus_size),
      /*crossover_level=*/absl::GetFlag(FLAGS_crossover_level),
      /*use_pc_features=*/absl::GetFlag(FLAGS_use_pc_features),
      /*path_level=*/absl::GetFlag(FLAGS_path_level),
      /*use_cmp_features=*/absl::GetFlag(FLAGS_use_cmp_features),
      /*callstack_level=*/absl::GetFlag(FLAGS_callstack_level),
      /*use_auto_dictionary=*/absl::GetFlag(FLAGS_use_auto_dictionary),
      /*use_dataflow_features=*/absl::GetFlag(FLAGS_use_dataflow_features),
      /*use_counter_features=*/absl::GetFlag(FLAGS_use_counter_features),
      /*use_pcpair_features=*/absl::GetFlag(FLAGS_use_pcpair_features),
      /*user_feature_domain_mask=*/
      absl::GetFlag(FLAGS_user_feature_domain_mask),
      /*feature_frequency_threshold=*/
      absl::GetFlag(FLAGS_feature_frequency_threshold),
      /*require_pc_table=*/absl::GetFlag(FLAGS_require_pc_table),
      /*require_seeds=*/absl::GetFlag(FLAGS_require_seeds),
      /*telemetry_frequency=*/absl::GetFlag(FLAGS_telemetry_frequency),
      /*print_runner_log=*/absl::GetFlag(FLAGS_print_runner_log),
      /*distill=*/absl::GetFlag(FLAGS_distill),
      /*log_features_shards=*/absl::GetFlag(FLAGS_log_features_shards),
      /*knobs_file=*/absl::GetFlag(FLAGS_knobs_file),
      /*corpus_to_files=*/absl::GetFlag(FLAGS_corpus_to_files),
      /*corpus_from_files=*/absl::GetFlag(FLAGS_corpus_from_files),
      /*corpus_dir=*/absl::GetFlag(FLAGS_corpus_dir),
      /*symbolizer_path=*/absl::GetFlag(FLAGS_symbolizer_path),
      /*objdump_path=*/absl::GetFlag(FLAGS_objdump_path),
      /*runner_dl_path_suffix=*/absl::GetFlag(FLAGS_runner_dl_path_suffix),
      /*input_filter=*/absl::GetFlag(FLAGS_input_filter),
      /*dictionary=*/absl::GetFlag(FLAGS_dictionary),
      /*function_filter=*/absl::GetFlag(FLAGS_function_filter),
      /*for_each_blob=*/absl::GetFlag(FLAGS_for_each_blob),
      /*experiment=*/absl::GetFlag(FLAGS_experiment),
      /*analyze=*/absl::GetFlag(FLAGS_analyze),
      /*exit_on_crash=*/absl::GetFlag(FLAGS_exit_on_crash),
      /*max_num_crash_reports=*/absl::GetFlag(FLAGS_num_crash_reports),
      /*minimize_crash_file_path=*/absl::GetFlag(FLAGS_minimize_crash),
      /*batch_triage_suspect_only=*/
      absl::GetFlag(FLAGS_batch_triage_suspect_only),
      /*shmem_size_mb=*/absl::GetFlag(FLAGS_shmem_size_mb),
      /*use_posix_shmem=*/absl::GetFlag(FLAGS_use_posix_shmem),
      /*dry_run=*/absl::GetFlag(FLAGS_dry_run),
      /*save_binary_info=*/absl::GetFlag(FLAGS_save_binary_info),
      /*populate_binary_info=*/absl::GetFlag(FLAGS_populate_binary_info),
#ifdef CENTIPEDE_DISABLE_RIEGELI
      /*riegeli=*/false,
#else
      /*riegeli=*/absl::GetFlag(FLAGS_riegeli),
#endif  // CENTIPEDE_DISABLE_RIEGELI
      /*first_corpus_dir_output_only=*/
      Environment::Default().first_corpus_dir_output_only,
      /*load_shards_only=*/Environment::Default().load_shards_only,
      /*fuzztest_single_test_mode=*/
      Environment::Default().fuzztest_single_test_mode,
      /*fuzztest_configuration=*/Environment::Default().fuzztest_configuration,
      /*list_crash_ids=*/Environment::Default().list_crash_ids,
      /*list_crash_ids_file=*/Environment::Default().list_crash_ids_file,
      /*crash_id=*/Environment::Default().crash_id,
      /*replay_crash=*/Environment::Default().replay_crash,
      /*export_crash=*/Environment::Default().export_crash,
      /*export_crash_file=*/Environment::Default().export_crash_file,
      /*exec_name=*/Environment::Default().exec_name,
      /*args=*/Environment::Default().args,
      /*binary_name=*/
      std::filesystem::path(coverage_binary).filename().string(),
      /*binary_hash=*/absl::GetFlag(FLAGS_binary_hash),
  };

  env_from_flags.UpdateBinaryHashIfEmpty();

  env_from_flags.UpdateTimeoutPerBatchIfEqualTo(
      Environment::Default().timeout_per_batch);

  if (size_t j = absl::GetFlag(FLAGS_j)) {
    env_from_flags.total_shards = j;
    env_from_flags.num_threads = j;
    env_from_flags.my_shard_index = 0;
  }
  CHECK_GE(env_from_flags.total_shards, 1);
  CHECK_GE(env_from_flags.batch_size, 1);
  CHECK_GE(env_from_flags.num_threads, 1);
  CHECK_LE(env_from_flags.num_threads, env_from_flags.total_shards);
  CHECK_LE(env_from_flags.my_shard_index + env_from_flags.num_threads,
           env_from_flags.total_shards)
      << VV(env_from_flags.my_shard_index) << VV(env_from_flags.num_threads);

  if (!argv.empty()) {
    env_from_flags.exec_name = argv[0];
    for (size_t i = 1; i < argv.size(); ++i) {
      env_from_flags.args.emplace_back(argv[i]);
    }
  }

  if (!env_from_flags.clang_coverage_binary.empty())
    env_from_flags.extra_binaries.push_back(
        env_from_flags.clang_coverage_binary);

  if (absl::StrContains(binary, "@@")) {
    LOG(INFO) << "@@ detected; running in standalone mode with batch_size=1";
    env_from_flags.has_input_wildcards = true;
    env_from_flags.batch_size = 1;
    // TODO(kcc): do we need to check if extra_binaries have @@?
  }

  env_from_flags.ReadKnobsFileIfSpecified();
  return env_from_flags;
}

}  // namespace fuzztest::internal
