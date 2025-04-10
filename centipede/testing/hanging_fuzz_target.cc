// Copyright 2025 The Centipede Authors.
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

#include <csignal>
#include <cstddef>
#include <cstdint>

// A fuzz target that hangs even during abort.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  signal(SIGABRT, SIG_IGN);  // Ignore abort signals and let the process hang.
  while (true) {
  }
  return 0;
}
