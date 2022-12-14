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

#ifndef MOZC_SESSION_RANDOM_KEYEVENTS_GENERATOR_H_
#define MOZC_SESSION_RANDOM_KEYEVENTS_GENERATOR_H_

#include <cstdint>
#include <vector>

#include "base/port.h"
#include "protocol/commands.pb.h"

namespace mozc {
namespace session {
class RandomKeyEventsGenerator {
 public:
  // Load all data to avoid a increse of memory usage
  // during memory leak tests.
  static void PrepareForMemoryLeakTest();

  // return test sentence set embedded in RandomKeyEventsGenerator.
  // Example:
  // const char **sentences = GetTestSentences(&size);
  // const char *s = sentences[10];
  static const char **GetTestSentences(size_t *test_size);

  // Generate a random test keyevents sequence for desktop
  static void GenerateSequence(std::vector<commands::KeyEvent> *keys);

  // Initialize random seed for this module.
  static void InitSeed(uint32_t seed);

  // Generate a random test keyevents sequence for mobile
  static void GenerateMobileSequence(bool create_probable_key_events,
                                     std::vector<commands::KeyEvent> *keys);
};
}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_RANDOM_KEYEVENTS_GENERATOR_H_
