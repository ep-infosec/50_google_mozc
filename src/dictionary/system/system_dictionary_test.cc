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

#include "dictionary/system/system_dictionary.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/file_util.h"
#include "base/logging.h"
#include "base/port.h"
#include "base/system_util.h"
#include "base/util.h"
#include "config/config_handler.h"
#include "data_manager/testing/mock_data_manager.h"
#include "dictionary/dictionary_test_util.h"
#include "dictionary/dictionary_token.h"
#include "dictionary/pos_matcher.h"
#include "dictionary/system/codec_interface.h"
#include "dictionary/system/system_dictionary_builder.h"
#include "dictionary/text_dictionary_loader.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "testing/base/public/googletest.h"
#include "testing/base/public/gunit.h"
#include "testing/base/public/mozctest.h"
#include "absl/container/btree_set.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

ABSL_FLAG(int32_t, dictionary_test_size, 100000,
          "Dictionary size for this test.");
ABSL_FLAG(int32_t, dictionary_reverse_lookup_test_size, 1000,
          "Number of tokens to run reverse lookup test.");
ABSL_DECLARE_FLAG(int32_t, min_key_length_to_use_small_cost_encoding);

namespace mozc {
namespace dictionary {
namespace {

class SystemDictionaryTest : public ::testing::Test {
 protected:
  SystemDictionaryTest()
      : pos_matcher_(mock_data_manager_.GetPosMatcherData()),
        text_dict_(pos_matcher_),
        dic_fn_(
            FileUtil::JoinPath(absl::GetFlag(FLAGS_test_tmpdir), "mozc.dic")) {
    const std::string dic_path = mozc::testing::GetSourceFileOrDie(
        {"data", "dictionary_oss", "dictionary00.txt"});
    text_dict_.LoadWithLineLimit(dic_path, "",
                                 absl::GetFlag(FLAGS_dictionary_test_size));

    convreq_.set_request(&request_);
    convreq_.set_config(&config_);
  }

  void SetUp() override {
    // Don't use small cost encoding by default.
    original_flags_min_key_length_to_use_small_cost_encoding_ =
        absl::GetFlag(FLAGS_min_key_length_to_use_small_cost_encoding);
    absl::SetFlag(&FLAGS_min_key_length_to_use_small_cost_encoding,
                  std::numeric_limits<int32_t>::max());

    request_.Clear();
    config::ConfigHandler::GetDefaultConfig(&config_);
  }

  void TearDown() override {
    absl::SetFlag(&FLAGS_min_key_length_to_use_small_cost_encoding,
                  original_flags_min_key_length_to_use_small_cost_encoding_);

    // This config initialization will be removed once ConversionRequest can
    // take config as an injected argument.
    config::Config config;
    config::ConfigHandler::GetDefaultConfig(&config);
    config::ConfigHandler::SetConfig(config);
  }

  void BuildAndWriteSystemDictionary(const std::vector<Token *> &source,
                                     size_t num_tokens,
                                     const std::string &filename);
  std::unique_ptr<SystemDictionary> BuildSystemDictionary(
      const std::vector<Token *> &source,
      size_t num_tokens = std::numeric_limits<size_t>::max());
  bool CompareTokensForLookup(const Token &a, const Token &b,
                              bool reverse) const;

  const testing::ScopedTmpUserProfileDirectory scoped_profile_dir_;
  const testing::MockDataManager mock_data_manager_;
  dictionary::PosMatcher pos_matcher_;
  TextDictionaryLoader text_dict_;

  ConversionRequest convreq_;
  config::Config config_;
  commands::Request request_;
  const std::string dic_fn_;
  int original_flags_min_key_length_to_use_small_cost_encoding_;
};

Token *GetTokenPointer(Token &token) { return &token; }
Token *GetTokenPointer(const std::unique_ptr<Token> &token) {
  return token.get();
}

// Get pointers to the Tokens contained in `token_container`. Since the returned
// vector contains mutable pointers to the elements of `token_container`, it
// cannot be passed by const reference.
template <typename C>
std::vector<Token *> MakeTokenPointers(C *token_container) {
  std::vector<Token *> ptrs;
  std::transform(std::begin(*token_container), std::end(*token_container),
                 std::back_inserter(ptrs),
                 [](auto &token) { return GetTokenPointer(token); });
  return ptrs;
}

void SystemDictionaryTest::BuildAndWriteSystemDictionary(
    const std::vector<Token *> &source, size_t num_tokens,
    const std::string &filename) {
  SystemDictionaryBuilder builder;
  std::vector<Token *> tokens;
  tokens.reserve(std::min(source.size(), num_tokens));
  // Picks up first tokens.
  for (auto it = source.begin();
       tokens.size() < num_tokens && it != source.end(); ++it) {
    tokens.push_back(*it);
  }
  builder.BuildFromTokens(tokens);
  builder.WriteToFile(filename);
}

std::unique_ptr<SystemDictionary> SystemDictionaryTest::BuildSystemDictionary(
    const std::vector<Token *> &source, size_t num_tokens) {
  BuildAndWriteSystemDictionary(source, num_tokens, dic_fn_);
  return SystemDictionary::Builder(dic_fn_).Build().value();
}

// Returns true if they seem to be same
bool SystemDictionaryTest::CompareTokensForLookup(const Token &a,
                                                  const Token &b,
                                                  bool reverse) const {
  const bool key_value_check = reverse ? (a.key == b.value && a.value == b.key)
                                       : (a.key == b.key && a.value == b.value);
  if (!key_value_check) {
    return false;
  }
  const bool comp_cost = a.cost == b.cost;
  if (!comp_cost) {
    return false;
  }
  const bool spelling_match = (a.attributes & Token::SPELLING_CORRECTION) ==
                              (b.attributes & Token::SPELLING_CORRECTION);
  if (!spelling_match) {
    return false;
  }
  const bool id_match = (a.lid == b.lid) && (a.rid == b.rid);
  if (!id_match) {
    return false;
  }
  return true;
}

TEST_F(SystemDictionaryTest, HasValue) {
  std::vector<Token> tokens;
  for (int i = 0; i < 4; ++i) {
    tokens.emplace_back(absl::StrFormat("??????%d", i),
                        absl::StrFormat("????????????%d", i));
  }

  const std::string kFull = "????????????";
  const std::string kHiragana = "????????????";
  const std::string kKatakanaKey = "????????????";
  const std::string kKatakanaValue = "????????????";

  tokens.emplace_back("Mozc", "Mozc");                // Alphabet
  tokens.emplace_back("upper", "UPPER");              // Alphabet upper case
  tokens.emplace_back("full", kFull);                 // Alphabet full width
  tokens.emplace_back(kHiragana, kHiragana);          // Hiragana
  tokens.emplace_back(kKatakanaKey, kKatakanaValue);  // Katakana

  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(MakeTokenPointers(&tokens));
  ASSERT_TRUE(system_dic.get() != nullptr);

  EXPECT_TRUE(system_dic->HasValue("????????????0"));
  EXPECT_TRUE(system_dic->HasValue("????????????1"));
  EXPECT_TRUE(system_dic->HasValue("????????????2"));
  EXPECT_TRUE(system_dic->HasValue("????????????3"));
  EXPECT_FALSE(system_dic->HasValue("????????????4"));
  EXPECT_FALSE(system_dic->HasValue("????????????5"));
  EXPECT_FALSE(system_dic->HasValue("????????????6"));

  EXPECT_TRUE(system_dic->HasValue("Mozc"));
  EXPECT_FALSE(system_dic->HasValue("mozc"));

  EXPECT_TRUE(system_dic->HasValue("UPPER"));
  EXPECT_FALSE(system_dic->HasValue("upper"));

  EXPECT_TRUE(system_dic->HasValue(kFull));
  EXPECT_FALSE(system_dic->HasValue("full"));

  EXPECT_TRUE(system_dic->HasValue(kHiragana));
  EXPECT_FALSE(system_dic->HasValue("????????????\n"));

  EXPECT_TRUE(system_dic->HasValue(kKatakanaValue));
  EXPECT_FALSE(system_dic->HasValue(kKatakanaKey));
}

TEST_F(SystemDictionaryTest, NormalWord) {
  Token token = {"???", "???", 100, 50, 70, Token::NONE};
  std::unique_ptr<SystemDictionary> system_dic = BuildSystemDictionary(
      {&token}, absl::GetFlag(FLAGS_dictionary_test_size));
  ASSERT_TRUE(system_dic);

  CollectTokenCallback callback;

  // Look up by exact key.
  system_dic->LookupPrefix(token.key, convreq_, &callback);
  ASSERT_EQ(1, callback.tokens().size());
  EXPECT_TOKEN_EQ(token, callback.tokens().front());

  // Look up by prefix.
  callback.Clear();
  system_dic->LookupPrefix("?????????", convreq_, &callback);
  ASSERT_EQ(1, callback.tokens().size());
  EXPECT_TOKEN_EQ(token, callback.tokens().front());

  // Nothing should be looked up.
  callback.Clear();
  system_dic->LookupPrefix("?????????", convreq_, &callback);
  EXPECT_TRUE(callback.tokens().empty());
}

TEST_F(SystemDictionaryTest, SameWord) {
  std::vector<Token> tokens = {
      {"???", "???", 100, 50, 70, Token::NONE},
      {"???", "???", 150, 100, 200, Token::NONE},
      {"???", "???", 100, 1000, 2000, Token::NONE},
      {"???", "???", 1000, 2000, 3000, Token::NONE},
  };

  std::vector<Token *> source_tokens = MakeTokenPointers(&tokens);
  std::unique_ptr<SystemDictionary> system_dic = BuildSystemDictionary(
      source_tokens, absl::GetFlag(FLAGS_dictionary_test_size));
  ASSERT_TRUE(system_dic);

  // All the tokens should be looked up.
  CollectTokenCallback callback;
  system_dic->LookupPrefix("???", convreq_, &callback);
  EXPECT_TOKENS_EQ_UNORDERED(source_tokens, callback.tokens());
}

TEST_F(SystemDictionaryTest, LookupAllWords) {
  const std::vector<std::unique_ptr<Token>> &source_tokens =
      text_dict_.tokens();
  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(MakeTokenPointers(&source_tokens),
                            absl::GetFlag(FLAGS_dictionary_test_size));
  ASSERT_TRUE(system_dic);

  // All the tokens should be looked up.
  for (size_t i = 0; i < source_tokens.size(); ++i) {
    CheckTokenExistenceCallback callback(source_tokens[i].get());
    system_dic->LookupPrefix(source_tokens[i]->key, convreq_, &callback);
    EXPECT_TRUE(callback.found())
        << "Token was not found: " << PrintToken(*source_tokens[i]);
  }
}

TEST_F(SystemDictionaryTest, SimpleLookupPrefix) {
  const std::string k0 = "???";
  const std::string k1 = "???????????????";
  Token t0 = {k0, "aa", 0, 0, 0, Token::NONE};
  Token t1 = {k1, "bb", 0, 0, 0, Token::NONE};

  std::vector<Token *> source_tokens = {&t0, &t1};
  text_dict_.CollectTokens(&source_tokens);
  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(source_tokens, 100);
  ASSERT_TRUE(system_dic);

  // |t0| should be looked up from |k1|.
  CheckTokenExistenceCallback callback(&t0);
  system_dic->LookupPrefix(k1, convreq_, &callback);
  EXPECT_TRUE(callback.found());
}

class LookupPrefixTestCallback : public SystemDictionary::Callback {
 public:
  ResultType OnKey(absl::string_view key) override {
    if (key == "??????") {
      return TRAVERSE_CULL;
    } else if (key == "???") {
      return TRAVERSE_NEXT_KEY;
    } else if (key == "???") {
      return TRAVERSE_DONE;
    }
    return TRAVERSE_CONTINUE;
  }

  ResultType OnToken(absl::string_view key, absl::string_view actual_key,
                     const Token &token) override {
    result_.insert(std::make_pair(token.key, token.value));
    return TRAVERSE_CONTINUE;
  }

  const std::set<std::pair<std::string, std::string>> &result() const {
    return result_;
  }

 private:
  std::set<std::pair<std::string, std::string>> result_;
};

TEST_F(SystemDictionaryTest, LookupPrefix) {
  // Set up a test dictionary.
  struct {
    const char *key;
    const char *value;
  } kKeyValues[] = {
      {"???", "???"},       {"???", "???"},         {"???", "???"},
      {"??????", "???"},     {"??????", "???"},       {"?????????", "??????"},
      {"???", "???"},       {"??????", "??????"},     {"??????", "??????"},
      {"?????????", "??????"}, {"???", "???"},         {"???", "???"},
      {"??????", "???"},     {"???", "???"},         {"???", "???"},
      {"??????", "??????"},   {"?????????", "?????????"}, {"???", "???"},
      {"???", "???"},       {"??????", "??????"},     {"???", "???"},
      {"??????", "??????"},   {"??????", "??????"},     {"?????????", "?????????"},
  };
  constexpr size_t kKeyValuesSize = std::size(kKeyValues);
  std::vector<Token> tokens;
  tokens.reserve(kKeyValuesSize);
  for (const auto &kv : kKeyValues) {
    tokens.emplace_back(kv.key, kv.value);
  }
  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(MakeTokenPointers(&tokens), kKeyValuesSize);
  ASSERT_TRUE(system_dic);

  // Test for normal prefix lookup without key expansion.
  {
    LookupPrefixTestCallback callback;
    system_dic->LookupPrefix("??????",  // "??????"
                             convreq_, &callback);
    const std::set<std::pair<std::string, std::string>> &result =
        callback.result();
    // "???" -- "??????" should be found.
    for (size_t i = 0; i < 5; ++i) {
      const std::pair<std::string, std::string> entry(kKeyValues[i].key,
                                                      kKeyValues[i].value);
      EXPECT_TRUE(result.end() != result.find(entry));
    }
    // The others should not be found.
    for (size_t i = 5; i < std::size(kKeyValues); ++i) {
      const std::pair<std::string, std::string> entry(kKeyValues[i].key,
                                                      kKeyValues[i].value);
      EXPECT_TRUE(result.end() == result.find(entry));
    }
  }

  // Test for normal prefix lookup without key expansion, but with culling
  // feature.
  {
    LookupPrefixTestCallback callback;
    system_dic->LookupPrefix("?????????", convreq_, &callback);
    const std::set<std::pair<std::string, std::string>> &result =
        callback.result();
    // Only "???" should be found as the callback doesn't traverse the subtree of
    // "??????" due to culling request from LookupPrefixTestCallback::OnKey().
    for (size_t i = 0; i < kKeyValuesSize; ++i) {
      const std::pair<std::string, std::string> entry(kKeyValues[i].key,
                                                      kKeyValues[i].value);
      EXPECT_EQ(entry.first == "???", result.find(entry) != result.end());
    }
  }

  // Test for TRAVERSE_NEXT_KEY.
  {
    LookupPrefixTestCallback callback;
    system_dic->LookupPrefix("?????????", convreq_, &callback);
    const std::set<std::pair<std::string, std::string>> &result =
        callback.result();
    // Only "??????" should be found as tokens for "???" is skipped (see
    // LookupPrefixTestCallback::OnKey()).
    for (size_t i = 0; i < kKeyValuesSize; ++i) {
      const std::pair<std::string, std::string> entry(kKeyValues[i].key,
                                                      kKeyValues[i].value);
      EXPECT_EQ(entry.first == "??????", result.find(entry) != result.end());
    }
  }

  // Test for TRAVERSE_DONE.
  {
    LookupPrefixTestCallback callback;
    system_dic->LookupPrefix("?????????", convreq_, &callback);
    const std::set<std::pair<std::string, std::string>> &result =
        callback.result();
    // Nothing should be found as the traversal is immediately done after seeing
    // "???"; see LookupPrefixTestCallback::OnKey().
    EXPECT_TRUE(result.empty());
  }

  // Test for prefix lookup with key expansion.
  {
    LookupPrefixTestCallback callback;
    // Use kana modifier insensitive lookup
    request_.set_kana_modifier_insensitive_conversion(true);
    config_.set_use_kana_modifier_insensitive_conversion(true);
    system_dic->LookupPrefix("??????", convreq_, &callback);
    const std::set<std::pair<std::string, std::string>> &result =
        callback.result();
    const char *kExpectedKeys[] = {
        "???", "???", "??????", "??????", "??????", "??????",
    };
    const absl::btree_set<std::string> expected(
        kExpectedKeys, kExpectedKeys + std::size(kExpectedKeys));
    for (size_t i = 0; i < kKeyValuesSize; ++i) {
      const bool to_be_found =
          expected.find(kKeyValues[i].key) != expected.end();
      const std::pair<std::string, std::string> entry(kKeyValues[i].key,
                                                      kKeyValues[i].value);
      EXPECT_EQ(to_be_found, result.find(entry) != result.end());
    }
  }
}

TEST_F(SystemDictionaryTest, LookupPredictive) {
  Token tokens[] = {
      {"??????????????????", "value0", 0, 0, 0, Token::NONE},
      {"????????????????????????", "value1", 0, 0, 0, Token::NONE},
  };

  // Build a dictionary with the above two tokens plus those from test data.
  std::vector<Token *> source_tokens = MakeTokenPointers(&tokens);
  text_dict_.CollectTokens(&source_tokens);  // Load test data.
  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(source_tokens, 10000);
  ASSERT_TRUE(system_dic);

  // All the tokens in |tokens| should be looked up by "???????????????".
  constexpr char kMamimumemo[] = "???????????????";
  CheckMultiTokensExistenceCallback callback({&tokens[0], &tokens[1]});
  system_dic->LookupPredictive(kMamimumemo, convreq_, &callback);
  EXPECT_TRUE(callback.AreAllFound());
}

TEST_F(SystemDictionaryTest, LookupPredictiveKanaModifierInsensitiveLookup) {
  Token tokens[] = {
      {"????????????", "??????", 0, 0, 0, Token::NONE},
      {"????????????", "??????", 0, 0, 0, Token::NONE},
  };
  const std::vector<Token *> source_tokens = {&tokens[0], &tokens[1]};
  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(source_tokens, 100);
  ASSERT_TRUE(system_dic);

  const std::string kKey = "????????????";

  // Without Kana modifier insensitive lookup flag, nothing is looked up.
  CollectTokenCallback callback;
  request_.set_kana_modifier_insensitive_conversion(false);
  config_.set_use_kana_modifier_insensitive_conversion(false);
  system_dic->LookupPredictive(kKey, convreq_, &callback);
  EXPECT_TRUE(callback.tokens().empty());

  // With Kana modifier insensitive lookup flag, every token is looked up.
  callback.Clear();
  request_.set_kana_modifier_insensitive_conversion(true);
  config_.set_use_kana_modifier_insensitive_conversion(true);
  system_dic->LookupPredictive(kKey, convreq_, &callback);
  EXPECT_TOKENS_EQ_UNORDERED(source_tokens, callback.tokens());
}

TEST_F(SystemDictionaryTest, LookupPredictiveCutOffEmulatingBFS) {
  Token tokens[] = {
      {"??????", "ai", 0, 0, 0, Token::NONE},
      {"???????????????", "aiueo", 0, 0, 0, Token::NONE},
  };
  // Build a dictionary with the above two tokens plus those from test data.
  std::vector<Token *> source_tokens = MakeTokenPointers(&tokens);
  text_dict_.CollectTokens(&source_tokens);  // Load test data.
  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(source_tokens, 10000);
  ASSERT_TRUE(system_dic);

  // Since there are many entries starting with "???" in test dictionary, it's
  // expected that "???????????????" is not looked up because of longer key cut-off
  // mechanism.  However, "??????" is looked up as it's short.
  CheckMultiTokensExistenceCallback callback({&tokens[0], &tokens[1]});
  system_dic->LookupPredictive("???", convreq_, &callback);
  EXPECT_TRUE(callback.IsFound(&tokens[0]));
  EXPECT_FALSE(callback.IsFound(&tokens[1]));
}

TEST_F(SystemDictionaryTest, LookupExact) {
  const std::string k0 = "???";
  const std::string k1 = "???????????????";
  Token t0 = {k0, "aa", 0, 0, 0, Token::NONE};
  Token t1 = {k1, "bb", 0, 0, 0, Token::NONE};
  std::vector<Token *> source_tokens = {&t0, &t1};
  text_dict_.CollectTokens(&source_tokens);
  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(source_tokens, 100);
  ASSERT_TRUE(system_dic);

  // |t0| should not be looked up from |k1|.
  CheckTokenExistenceCallback callback0(&t0);
  system_dic->LookupExact(k1, convreq_, &callback0);
  EXPECT_FALSE(callback0.found());
  // But |t1| should be found.
  CheckTokenExistenceCallback callback1(&t1);
  system_dic->LookupExact(k1, convreq_, &callback1);
  EXPECT_TRUE(callback1.found());

  // Nothing should be found from "hoge".
  CollectTokenCallback callback_hoge;
  system_dic->LookupExact("hoge", convreq_, &callback_hoge);
  EXPECT_TRUE(callback_hoge.tokens().empty());
}

TEST_F(SystemDictionaryTest, LookupReverse) {
  Token tokens[] = {
      {"???", "???", 1, 2, 3, Token::NONE},
      {"???????????????", "???????????????", 1, 2, 3, Token::NONE},
      {"?????????????????", "?????????????????", 1, 2, 3, Token::NONE},
      // Both token[3] and token[4] will be encoded into 3 bytes.
      {"??????????????????", "??????????????????", 32000, 1, 1, Token::NONE},
      {"??????????????????", "??????????????????", 32000, 1, 2, Token::NONE},
      // token[5] will be encoded into 3 bytes.
      {"??????????????????", "??????????????????", 32000, 1, 1, Token::NONE},
      {"???????????????", "???????????????", 1, 2, 3, Token::SPELLING_CORRECTION},
      {"???????????????", "???????????????", 1, 1, 1, Token::NONE},
      {"???????????????", "???????????????", 1, 1, 1, Token::NONE},
  };
  std::vector<Token *> source_tokens = MakeTokenPointers(&tokens);
  text_dict_.CollectTokens(&source_tokens);
  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(source_tokens, source_tokens.size());
  ASSERT_TRUE(system_dic);

  const size_t test_size =
      std::min<size_t>(absl::GetFlag(FLAGS_dictionary_reverse_lookup_test_size),
                       source_tokens.size());
  for (size_t source_index = 0; source_index < test_size; ++source_index) {
    const Token &source_token = *source_tokens[source_index];
    CollectTokenCallback callback;
    system_dic->LookupReverse(source_token.value, convreq_, &callback);

    bool found = false;
    for (const Token &token : callback.tokens()) {
      // Make sure any of the key lengths of the lookup results
      // doesn't exceed the original key length.
      // It happened once
      // when called with "???????????????", returning "??????????????????".
      EXPECT_LE(token.key.size(), source_token.value.size())
          << token.key << ":" << token.value << "\t" << source_token.value;
      if (CompareTokensForLookup(source_token, token, true)) {
        found = true;
      }
    }

    if ((source_token.attributes & Token::SPELLING_CORRECTION) ==
        Token::SPELLING_CORRECTION) {
      EXPECT_FALSE(found) << "Spelling correction token was retrieved:"
                          << PrintToken(source_token);
      if (found) {
        return;
      }
    } else {
      EXPECT_TRUE(found) << "Failed to find " << source_token.key << ":"
                         << source_token.value;
      if (!found) {
        return;
      }
    }
  }

  {
    // test for non exact transliterated index string.
    // append "???"
    const std::string key = tokens[7].value + "???";
    CollectTokenCallback callback;
    system_dic->LookupReverse(key, convreq_, &callback);
    bool found = false;
    for (const Token &token : callback.tokens()) {
      if (CompareTokensForLookup(tokens[7], token, true)) {
        found = true;
      }
    }
    EXPECT_TRUE(found) << "Missed token for non exact transliterated index "
                       << key;
  }
}

TEST_F(SystemDictionaryTest, LookupReverseIndex) {
  const std::vector<std::unique_ptr<Token>> &source_tokens =
      text_dict_.tokens();
  BuildAndWriteSystemDictionary(MakeTokenPointers(&source_tokens),
                                absl::GetFlag(FLAGS_dictionary_test_size),
                                dic_fn_);

  std::unique_ptr<SystemDictionary> system_dic_without_index =
      SystemDictionary::Builder(dic_fn_)
          .SetOptions(SystemDictionary::NONE)
          .Build()
          .value();
  ASSERT_TRUE(system_dic_without_index)
      << "Failed to open dictionary source:" << dic_fn_;
  std::unique_ptr<SystemDictionary> system_dic_with_index =
      SystemDictionary::Builder(dic_fn_)
          .SetOptions(SystemDictionary::ENABLE_REVERSE_LOOKUP_INDEX)
          .Build()
          .value();
  ASSERT_TRUE(system_dic_with_index)
      << "Failed to open dictionary source:" << dic_fn_;

  int size = absl::GetFlag(FLAGS_dictionary_reverse_lookup_test_size);
  for (auto it = source_tokens.begin(); size > 0 && it != source_tokens.end();
       ++it, --size) {
    const Token &t = **it;
    CollectTokenCallback callback1, callback2;
    system_dic_without_index->LookupReverse(t.value, convreq_, &callback1);
    system_dic_with_index->LookupReverse(t.value, convreq_, &callback2);

    const std::vector<Token> &tokens1 = callback1.tokens();
    const std::vector<Token> &tokens2 = callback2.tokens();
    ASSERT_EQ(tokens1.size(), tokens2.size());
    for (size_t i = 0; i < tokens1.size(); ++i) {
      EXPECT_TOKEN_EQ(tokens1[i], tokens2[i]);
    }
  }
}

TEST_F(SystemDictionaryTest, LookupReverseWithCache) {
  const std::string kDoraemon = "???????????????";

  Token source_token;
  source_token.key = "???????????????";
  source_token.value = kDoraemon;
  source_token.cost = 1;
  source_token.lid = 2;
  source_token.rid = 3;
  std::vector<Token *> source_tokens = {&source_token};
  text_dict_.CollectTokens(&source_tokens);
  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(source_tokens, source_tokens.size());
  ASSERT_TRUE(system_dic);
  system_dic->PopulateReverseLookupCache(kDoraemon);

  Token target_token = source_token;
  target_token.key.swap(target_token.value);

  CheckTokenExistenceCallback callback(&target_token);
  system_dic->LookupReverse(kDoraemon, convreq_, &callback);
  EXPECT_TRUE(callback.found())
      << "Could not find " << PrintToken(source_token);
  system_dic->ClearReverseLookupCache();
}

TEST_F(SystemDictionaryTest, SpellingCorrectionTokens) {
  std::vector<Token> tokens = {
      {"????????????", "????????????", 1, 0, 2, Token::SPELLING_CORRECTION},
      {"????????????????????????", "????????????????????????", 1, 100, 3,
       Token::SPELLING_CORRECTION},
      {"???????????????", "?????????", 1000, 1, 2, Token::NONE},
  };

  std::vector<Token *> source_tokens = MakeTokenPointers(&tokens);
  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(source_tokens, source_tokens.size());
  ASSERT_TRUE(system_dic);

  for (size_t i = 0; i < source_tokens.size(); ++i) {
    CheckTokenExistenceCallback callback(source_tokens[i]);
    system_dic->LookupPrefix(source_tokens[i]->key, convreq_, &callback);
    EXPECT_TRUE(callback.found())
        << "Token " << i << " was not found: " << PrintToken(*source_tokens[i]);
  }
}

TEST_F(SystemDictionaryTest, EnableNoModifierTargetWithLoudsTrie) {
  const std::string k0 = "??????";
  const std::string k1 = "?????????";
  const std::string k2 = "????????????";
  const std::string k3 = "????????????";
  const std::string k4 = "????????????";

  Token tokens[5] = {
      {k0, "aa", 0, 0, 0, Token::NONE}, {k1, "bb", 0, 0, 0, Token::NONE},
      {k2, "cc", 0, 0, 0, Token::NONE}, {k3, "dd", 0, 0, 0, Token::NONE},
      {k4, "ee", 0, 0, 0, Token::NONE},
  };

  std::vector<Token *> source_tokens = MakeTokenPointers(&tokens);
  text_dict_.CollectTokens(&source_tokens);
  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(source_tokens, 100);
  ASSERT_TRUE(system_dic);

  request_.set_kana_modifier_insensitive_conversion(true);
  config_.set_use_kana_modifier_insensitive_conversion(true);

  // Prefix search
  for (size_t i = 0; i < std::size(tokens); ++i) {
    CheckTokenExistenceCallback callback(&tokens[i]);
    // "????????????" -> "??????", "?????????", "????????????", "????????????" and "????????????"
    system_dic->LookupPrefix(k2, convreq_, &callback);
    EXPECT_TRUE(callback.found())
        << "Token " << i << " was not found: " << PrintToken(tokens[i]);
  }

  // Predictive searches
  {
    // "??????" -> "??????", "?????????", "????????????", "????????????" and "????????????"
    std::vector<Token *> expected = MakeTokenPointers(&tokens);
    CheckMultiTokensExistenceCallback callback(expected);
    system_dic->LookupPredictive(k0, convreq_, &callback);
    EXPECT_TRUE(callback.AreAllFound());
  }
  {
    // "?????????" -> "?????????", "????????????" and "????????????"
    std::vector<Token *> expected = {&tokens[1], &tokens[3], &tokens[4]};
    CheckMultiTokensExistenceCallback callback(expected);
    system_dic->LookupPredictive(k1, convreq_, &callback);
    EXPECT_TRUE(callback.AreAllFound());
  }
}

TEST_F(SystemDictionaryTest, NoModifierForKanaEntries) {
  Token t0 = {"?????????????????????", "?????????????????????", 0, 0, 0, Token::NONE};
  Token t1 = {"???????????????", "???????????????", 0, 0, 0, Token::NONE};

  std::vector<Token *> source_tokens = {&t0, &t1};
  text_dict_.CollectTokens(&source_tokens);
  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(source_tokens, 100);
  ASSERT_TRUE(system_dic);

  // Lookup |t0| from "?????????????????????"
  const std::string k = "?????????????????????";
  request_.set_kana_modifier_insensitive_conversion(true);
  config_.set_use_kana_modifier_insensitive_conversion(true);
  CheckTokenExistenceCallback callback(&t0);
  system_dic->LookupPrefix(k, convreq_, &callback);
  EXPECT_TRUE(callback.found()) << "Not found: " << PrintToken(t0);
}

TEST_F(SystemDictionaryTest, DoNotReturnNoModifierTargetWithLoudsTrie) {
  const std::string k0 = "??????";
  const std::string k1 = "?????????";
  const std::string k2 = "????????????";
  const std::string k3 = "????????????";
  const std::string k4 = "????????????";

  Token tokens[5] = {
      {k0, "aa", 0, 0, 0, Token::NONE}, {k1, "bb", 0, 0, 0, Token::NONE},
      {k2, "cc", 0, 0, 0, Token::NONE}, {k3, "dd", 0, 0, 0, Token::NONE},
      {k4, "ee", 0, 0, 0, Token::NONE},
  };
  std::vector<Token *> source_tokens = MakeTokenPointers(&tokens);
  text_dict_.CollectTokens(&source_tokens);
  std::unique_ptr<SystemDictionary> system_dic =
      BuildSystemDictionary(source_tokens, 100);
  ASSERT_TRUE(system_dic);

  request_.set_kana_modifier_insensitive_conversion(false);
  config_.set_use_kana_modifier_insensitive_conversion(false);

  // Prefix search
  {
    // "????????????" (k3) -> "?????????" (k1) and "????????????" (k3)
    // Make sure "????????????" is not in the results when searched by "????????????"
    std::vector<Token *> to_be_looked_up = {&tokens[1], &tokens[3]};
    std::vector<Token *> not_to_be_looked_up = {&tokens[0], &tokens[2],
                                                &tokens[4]};
    for (size_t i = 0; i < to_be_looked_up.size(); ++i) {
      CheckTokenExistenceCallback callback(to_be_looked_up[i]);
      system_dic->LookupPrefix(k3, convreq_, &callback);
      EXPECT_TRUE(callback.found())
          << "Token is not found: " << PrintToken(*to_be_looked_up[i]);
    }
    for (size_t i = 0; i < not_to_be_looked_up.size(); ++i) {
      CheckTokenExistenceCallback callback(not_to_be_looked_up[i]);
      system_dic->LookupPrefix(k3, convreq_, &callback);
      EXPECT_FALSE(callback.found()) << "Token should not be found: "
                                     << PrintToken(*not_to_be_looked_up[i]);
    }
  }

  // Predictive search
  {
    // "?????????" -> "?????????" and "????????????"
    // Make sure "????????????" is not in the results when searched by "?????????"
    std::vector<Token *> to_be_looked_up = {&tokens[1], &tokens[3]};
    std::vector<Token *> not_to_be_looked_up = {&tokens[0], &tokens[2],
                                                &tokens[4]};
    for (size_t i = 0; i < to_be_looked_up.size(); ++i) {
      CheckTokenExistenceCallback callback(to_be_looked_up[i]);
      system_dic->LookupPredictive(k1, convreq_, &callback);
      EXPECT_TRUE(callback.found())
          << "Token is not found: " << PrintToken(*to_be_looked_up[i]);
    }
    for (size_t i = 0; i < not_to_be_looked_up.size(); ++i) {
      CheckTokenExistenceCallback callback(not_to_be_looked_up[i]);
      system_dic->LookupPredictive(k3, convreq_, &callback);
      EXPECT_FALSE(callback.found()) << "Token should not be found: "
                                     << PrintToken(*not_to_be_looked_up[i]);
    }
  }
}

}  // namespace
}  // namespace dictionary
}  // namespace mozc
