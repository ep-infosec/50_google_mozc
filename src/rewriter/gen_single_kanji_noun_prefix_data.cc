// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/init_mozc.h"
#include "base/port.h"
#include "data_manager/serialized_dictionary.h"
#include "absl/flags/flag.h"

ABSL_FLAG(std::string, output_token_array, "",
          "Output token array of noun prefix dictionary");
ABSL_FLAG(std::string, output_string_array, "",
          "Output string array of noun prefix dictionary");

namespace {

struct NounPrefix {
  const char *key;
  const char *value;
  int16_t rank;
} kNounPrefixList[] = {
    {"???", "???", 1},
    {"???", "???", 1},
    // {"???", "???"},    // don't register it as ??? isn't in the ipadic.
    // {"???", "???"},    // seems to be rare.
    {"??????", "??????", 1},
    {"??????", "???", 1},
    {"??????", "???", 0},
    {"??????", "???", 1},
    {"??????", "???", 0},
    {"??????", "???", 0},
    {"??????", "???", 1},
    {"??????", "???", 0},
    {"??????", "???", 1},
    {"??????", "???", 1},
    {"??????", "???", 1},
    {"??????", "???", 1},
    {"??????", "???", 1},
    {"??????", "???", 1},
    {"???", "???", 1},
    {"???", "???", 1},
    {"??????", "???", 1},
    {"??????", "???", 1},
    {"???", "???", 0},
    {"??????", "???", 1},
    {"???", "???", 0},
    {"??????", "???", 1},
    {"??????", "???", 1},
    {"??????", "???", 1},
    {"???", "???", 1},
    {"?????????", "???", 1},
    {"?????????", "???", 1},
    {"??????", "???", 1},
    {"???", "???", 1},
    {"??????", "???", 1},
};

}  // namespace

int main(int argc, char **argv) {
  mozc::InitMozc(argv[0], &argc, &argv);

  std::map<std::string, mozc::SerializedDictionary::TokenList> tokens;
  for (const NounPrefix &entry : kNounPrefixList) {
    std::unique_ptr<mozc::SerializedDictionary::CompilerToken> token(
        new mozc::SerializedDictionary::CompilerToken);
    token->value = entry.value;
    token->lid = 0;
    token->rid = 0;
    token->cost = entry.rank;
    tokens[entry.key].emplace_back(std::move(token));
  }
  mozc::SerializedDictionary::CompileToFiles(
      tokens, absl::GetFlag(FLAGS_output_token_array),
      absl::GetFlag(FLAGS_output_string_array));
  return 0;
}
