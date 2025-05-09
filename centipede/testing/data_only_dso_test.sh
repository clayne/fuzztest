#!/bin/bash

# Copyright 2024 The Centipede Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Tests basic functionality for a target with data-only DSOs.
set -eu

source "$(dirname "$0")/../test_util.sh"

CENTIPEDE_TEST_SRCDIR="$(fuzztest::internal::get_centipede_test_srcdir)"

fuzztest::internal::maybe_set_var_to_executable_path \
  CENTIPEDE_BINARY "${CENTIPEDE_TEST_SRCDIR}/centipede"

fuzztest::internal::maybe_set_var_to_executable_path \
  TARGET_BINARY "${CENTIPEDE_TEST_SRCDIR}/testing/data_only_dso_target"

fuzztest::internal::maybe_set_var_to_executable_path \
  LLVM_SYMBOLIZER "$(fuzztest::internal::get_llvm_symbolizer_path)"

# Run fuzzing until the first crash.
declare -r WD="${TEST_TMPDIR}/WD"
declare -r LOG="${TEST_TMPDIR}/log"
fuzztest::internal::ensure_empty_dir "${WD}"

"${CENTIPEDE_BINARY}" --binary="${TARGET_BINARY}" --workdir="${WD}" \
  --exit_on_crash=1 --seed=1 --log_features_shards=1 \
  --symbolizer_path="${LLVM_SYMBOLIZER}" \
  2>&1 | tee "${LOG}"

echo "Fuzzing DONE"

fuzztest::internal::assert_regex_in_file "Batch execution failed:" "${LOG}"
fuzztest::internal::assert_regex_in_file "Input bytes.*: GoCrash" "${LOG}"
fuzztest::internal::assert_regex_in_file "Symbolizing 1 instrumented DSOs" "${LOG}"

echo "PASS"
