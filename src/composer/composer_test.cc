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

#include "composer/composer.h"

#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/clock_mock.h"
#include "base/logging.h"
#include "base/system_util.h"
#include "base/util.h"
#include "composer/internal/typing_model.h"
#include "composer/key_parser.h"
#include "composer/table.h"
#include "config/character_form_manager.h"
#include "config/config_handler.h"
#include "data_manager/testing/mock_data_manager.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "testing/base/public/gunit.h"
#include "absl/strings/string_view.h"

namespace mozc {
namespace composer {
namespace {

using CharacterFormManager = ::mozc::config::CharacterFormManager;
using Config = ::mozc::config::Config;
using ConfigHandler = ::mozc::config::ConfigHandler;
using KeyEvent = ::mozc::commands::KeyEvent;
using ProbableKeyEvent = ::mozc::commands::KeyEvent::ProbableKeyEvent;
using ProbableKeyEvents = ::mozc::protobuf::RepeatedPtrField<ProbableKeyEvent>;
using Request = ::mozc::commands::Request;

bool InsertKey(const std::string &key_string, Composer *composer) {
  commands::KeyEvent key;
  if (!KeyParser::ParseKey(key_string, &key)) {
    return false;
  }
  return composer->InsertCharacterKeyEvent(key);
}

bool InsertKeyWithMode(const std::string &key_string,
                       const commands::CompositionMode mode,
                       Composer *composer) {
  commands::KeyEvent key;
  if (!KeyParser::ParseKey(key_string, &key)) {
    return false;
  }
  key.set_mode(mode);
  return composer->InsertCharacterKeyEvent(key);
}

std::string GetPreedit(const Composer *composer) {
  std::string preedit;
  composer->GetStringForPreedit(&preedit);
  return preedit;
}

void ExpectSameComposer(const Composer &lhs, const Composer &rhs) {
  EXPECT_EQ(lhs.GetCursor(), rhs.GetCursor());
  EXPECT_EQ(lhs.is_new_input(), rhs.is_new_input());
  EXPECT_EQ(lhs.GetInputMode(), rhs.GetInputMode());
  EXPECT_EQ(lhs.GetOutputMode(), rhs.GetOutputMode());
  EXPECT_EQ(lhs.GetComebackInputMode(), rhs.GetComebackInputMode());
  EXPECT_EQ(lhs.shifted_sequence_count(), rhs.shifted_sequence_count());
  EXPECT_EQ(lhs.source_text(), rhs.source_text());
  EXPECT_EQ(lhs.max_length(), rhs.max_length());
  EXPECT_EQ(lhs.GetInputFieldType(), rhs.GetInputFieldType());

  {
    std::string left_text, right_text;
    lhs.GetStringForPreedit(&left_text);
    rhs.GetStringForPreedit(&right_text);
    EXPECT_EQ(left_text, right_text);
  }
  {
    std::string left_text, right_text;
    lhs.GetStringForSubmission(&left_text);
    rhs.GetStringForSubmission(&right_text);
    EXPECT_EQ(left_text, right_text);
  }
  {
    std::string left_text, right_text;
    lhs.GetQueryForConversion(&left_text);
    rhs.GetQueryForConversion(&right_text);
    EXPECT_EQ(left_text, right_text);
  }
  {
    std::string left_text, right_text;
    lhs.GetQueryForPrediction(&left_text);
    rhs.GetQueryForPrediction(&right_text);
    EXPECT_EQ(left_text, right_text);
  }
}

}  // namespace

class ComposerTest : public ::testing::Test {
 protected:
  ComposerTest() = default;
  ~ComposerTest() override = default;

  void SetUp() override {
    table_ = std::make_unique<Table>();
    config_ = std::make_unique<Config>();
    request_ = std::make_unique<Request>();
    composer_ =
        std::make_unique<Composer>(table_.get(), request_.get(), config_.get());
    CharacterFormManager::GetCharacterFormManager()->SetDefaultRule();
  }

  void TearDown() override {
    CharacterFormManager::GetCharacterFormManager()->SetDefaultRule();
    composer_.reset();
    request_.reset();
    config_.reset();
    table_.reset();
  }

  const testing::MockDataManager mock_data_manager_;
  std::unique_ptr<Composer> composer_;
  std::unique_ptr<Table> table_;
  std::unique_ptr<Request> request_;
  std::unique_ptr<Config> config_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ComposerTest);
};

TEST_F(ComposerTest, Reset) {
  composer_->InsertCharacter("mozuku");

  composer_->SetInputMode(transliteration::HALF_ASCII);

  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetOutputMode());
  composer_->SetOutputMode(transliteration::HALF_ASCII);
  EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetOutputMode());

  composer_->SetInputFieldType(commands::Context::PASSWORD);
  composer_->Reset();

  EXPECT_TRUE(composer_->Empty());
  // The input mode ramains as the previous mode.
  EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());
  EXPECT_EQ(commands::Context::PASSWORD, composer_->GetInputFieldType());
  // The output mode should be reset.
  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetOutputMode());
}

TEST_F(ComposerTest, ResetInputMode) {
  composer_->InsertCharacter("mozuku");

  composer_->SetInputMode(transliteration::FULL_KATAKANA);
  composer_->SetTemporaryInputMode(transliteration::HALF_ASCII);
  composer_->ResetInputMode();

  EXPECT_FALSE(composer_->Empty());
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());
}

TEST_F(ComposerTest, Empty) {
  composer_->InsertCharacter("mozuku");
  EXPECT_FALSE(composer_->Empty());

  composer_->EditErase();
  EXPECT_TRUE(composer_->Empty());
}

TEST_F(ComposerTest, EnableInsert) {
  composer_->set_max_length(6);

  composer_->InsertCharacter("mozuk");
  EXPECT_EQ(5, composer_->GetLength());

  EXPECT_TRUE(composer_->EnableInsert());
  composer_->InsertCharacter("u");
  EXPECT_EQ(6, composer_->GetLength());

  EXPECT_FALSE(composer_->EnableInsert());
  composer_->InsertCharacter("!");
  EXPECT_EQ(6, composer_->GetLength());

  std::string result;
  composer_->GetQueryForConversion(&result);
  EXPECT_EQ("mozuku", result);

  composer_->Backspace();
  EXPECT_EQ(5, composer_->GetLength());
  EXPECT_TRUE(composer_->EnableInsert());
}

TEST_F(ComposerTest, BackSpace) {
  composer_->InsertCharacter("abc");

  composer_->Backspace();
  EXPECT_EQ(2, composer_->GetLength());
  EXPECT_EQ(2, composer_->GetCursor());
  std::string result;
  composer_->GetQueryForConversion(&result);
  EXPECT_EQ("ab", result);

  result.clear();
  composer_->GetQueryForConversion(&result);
  EXPECT_EQ("ab", result);

  composer_->MoveCursorToBeginning();
  EXPECT_EQ(0, composer_->GetCursor());

  composer_->Backspace();
  EXPECT_EQ(2, composer_->GetLength());
  EXPECT_EQ(0, composer_->GetCursor());
  result.clear();
  composer_->GetQueryForConversion(&result);
  EXPECT_EQ("ab", result);

  composer_->Backspace();
  EXPECT_EQ(2, composer_->GetLength());
  EXPECT_EQ(0, composer_->GetCursor());
  result.clear();
  composer_->GetQueryForConversion(&result);
  EXPECT_EQ("ab", result);
}

TEST_F(ComposerTest, OutputMode) {
  // This behaviour is based on Kotoeri

  table_->AddRule("a", "???", "");
  table_->AddRule("i", "???", "");
  table_->AddRule("u", "???", "");

  composer_->SetOutputMode(transliteration::HIRAGANA);

  composer_->InsertCharacter("a");
  composer_->InsertCharacter("i");
  composer_->InsertCharacter("u");

  std::string output;
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("?????????", output);

  composer_->SetOutputMode(transliteration::FULL_ASCII);
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("?????????", output);

  composer_->InsertCharacter("a");
  composer_->InsertCharacter("i");
  composer_->InsertCharacter("u");
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("??????????????????", output);
}

TEST_F(ComposerTest, OutputMode2) {
  // This behaviour is based on Kotoeri

  table_->AddRule("a", "???", "");
  table_->AddRule("i", "???", "");
  table_->AddRule("u", "???", "");

  composer_->InsertCharacter("a");
  composer_->InsertCharacter("i");
  composer_->InsertCharacter("u");

  std::string output;
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("?????????", output);

  composer_->MoveCursorLeft();
  composer_->SetOutputMode(transliteration::FULL_ASCII);
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("?????????", output);

  composer_->InsertCharacter("a");
  composer_->InsertCharacter("i");
  composer_->InsertCharacter("u");
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("??????????????????", output);
}

TEST_F(ComposerTest, GetTransliterations) {
  table_->AddRule("a", "???", "");
  table_->AddRule("i", "???", "");
  table_->AddRule("u", "???", "");
  table_->AddRule("A", "???", "");
  table_->AddRule("I", "???", "");
  table_->AddRule("U", "???", "");
  composer_->InsertCharacter("a");

  transliteration::Transliterations transliterations;
  composer_->GetTransliterations(&transliterations);
  EXPECT_EQ(transliteration::NUM_T13N_TYPES, transliterations.size());
  EXPECT_EQ("???", transliterations[transliteration::HIRAGANA]);
  EXPECT_EQ("???", transliterations[transliteration::FULL_KATAKANA]);
  EXPECT_EQ("a", transliterations[transliteration::HALF_ASCII]);
  EXPECT_EQ("???", transliterations[transliteration::FULL_ASCII]);
  EXPECT_EQ("???", transliterations[transliteration::HALF_KATAKANA]);

  composer_->Reset();
  ASSERT_TRUE(composer_->Empty());
  transliterations.clear();

  composer_->InsertCharacter("!");
  composer_->GetTransliterations(&transliterations);
  EXPECT_EQ(transliteration::NUM_T13N_TYPES, transliterations.size());
  // NOTE(komatsu): The duplication will be handled by the session layer.
  EXPECT_EQ("???", transliterations[transliteration::HIRAGANA]);
  EXPECT_EQ("???", transliterations[transliteration::FULL_KATAKANA]);
  EXPECT_EQ("!", transliterations[transliteration::HALF_ASCII]);
  EXPECT_EQ("???", transliterations[transliteration::FULL_ASCII]);
  EXPECT_EQ("!", transliterations[transliteration::HALF_KATAKANA]);

  composer_->Reset();
  ASSERT_TRUE(composer_->Empty());
  transliterations.clear();

  composer_->InsertCharacter("aIu");
  composer_->GetTransliterations(&transliterations);
  EXPECT_EQ(transliteration::NUM_T13N_TYPES, transliterations.size());
  EXPECT_EQ("?????????", transliterations[transliteration::HIRAGANA]);
  EXPECT_EQ("?????????", transliterations[transliteration::FULL_KATAKANA]);
  EXPECT_EQ("aIu", transliterations[transliteration::HALF_ASCII]);
  EXPECT_EQ("AIU", transliterations[transliteration::HALF_ASCII_UPPER]);
  EXPECT_EQ("aiu", transliterations[transliteration::HALF_ASCII_LOWER]);
  EXPECT_EQ("Aiu", transliterations[transliteration::HALF_ASCII_CAPITALIZED]);
  EXPECT_EQ("?????????", transliterations[transliteration::FULL_ASCII]);
  EXPECT_EQ("?????????", transliterations[transliteration::FULL_ASCII_UPPER]);
  EXPECT_EQ("?????????", transliterations[transliteration::FULL_ASCII_LOWER]);
  EXPECT_EQ("?????????",
            transliterations[transliteration::FULL_ASCII_CAPITALIZED]);
  EXPECT_EQ("?????????", transliterations[transliteration::HALF_KATAKANA]);

  // Transliterations for quote marks.  This is a test against
  // http://b/1581367
  composer_->Reset();
  ASSERT_TRUE(composer_->Empty());
  transliterations.clear();

  composer_->InsertCharacter("'\"`");
  composer_->GetTransliterations(&transliterations);
  EXPECT_EQ("'\"`", transliterations[transliteration::HALF_ASCII]);
  EXPECT_EQ("?????????", transliterations[transliteration::FULL_ASCII]);
}

TEST_F(ComposerTest, GetSubTransliterations) {
  table_->AddRule("ka", "???", "");
  table_->AddRule("n", "???", "");
  table_->AddRule("na", "???", "");
  table_->AddRule("da", "???", "");

  composer_->InsertCharacter("kanna");

  transliteration::Transliterations transliterations;
  composer_->GetSubTransliterations(0, 2, &transliterations);
  EXPECT_EQ("??????", transliterations[transliteration::HIRAGANA]);
  EXPECT_EQ("??????", transliterations[transliteration::FULL_KATAKANA]);
  EXPECT_EQ("kan", transliterations[transliteration::HALF_ASCII]);
  EXPECT_EQ("?????????", transliterations[transliteration::FULL_ASCII]);
  EXPECT_EQ("??????", transliterations[transliteration::HALF_KATAKANA]);

  transliterations.clear();
  composer_->GetSubTransliterations(1, 1, &transliterations);
  EXPECT_EQ("???", transliterations[transliteration::HIRAGANA]);
  EXPECT_EQ("???", transliterations[transliteration::FULL_KATAKANA]);
  EXPECT_EQ("n", transliterations[transliteration::HALF_ASCII]);
  EXPECT_EQ("???", transliterations[transliteration::FULL_ASCII]);
  EXPECT_EQ("???", transliterations[transliteration::HALF_KATAKANA]);

  transliterations.clear();
  composer_->GetSubTransliterations(2, 1, &transliterations);
  EXPECT_EQ("???", transliterations[transliteration::HIRAGANA]);
  EXPECT_EQ("???", transliterations[transliteration::FULL_KATAKANA]);
  EXPECT_EQ("na", transliterations[transliteration::HALF_ASCII]);
  EXPECT_EQ("??????", transliterations[transliteration::FULL_ASCII]);
  EXPECT_EQ("???", transliterations[transliteration::HALF_KATAKANA]);

  // Invalid position
  transliterations.clear();
  composer_->GetSubTransliterations(5, 3, &transliterations);
  EXPECT_EQ("", transliterations[transliteration::HIRAGANA]);
  EXPECT_EQ("", transliterations[transliteration::FULL_KATAKANA]);
  EXPECT_EQ("", transliterations[transliteration::HALF_ASCII]);
  EXPECT_EQ("", transliterations[transliteration::FULL_ASCII]);
  EXPECT_EQ("", transliterations[transliteration::HALF_KATAKANA]);

  // Invalid size
  transliterations.clear();
  composer_->GetSubTransliterations(0, 999, &transliterations);
  EXPECT_EQ("?????????", transliterations[transliteration::HIRAGANA]);
  EXPECT_EQ("?????????", transliterations[transliteration::FULL_KATAKANA]);
  EXPECT_EQ("kanna", transliterations[transliteration::HALF_ASCII]);
  EXPECT_EQ("???????????????", transliterations[transliteration::FULL_ASCII]);
  EXPECT_EQ("?????????", transliterations[transliteration::HALF_KATAKANA]);

  // Dakuon case
  transliterations.clear();
  composer_->EditErase();
  composer_->InsertCharacter("dankann");
  composer_->GetSubTransliterations(0, 3, &transliterations);
  EXPECT_EQ("?????????", transliterations[transliteration::HIRAGANA]);
  EXPECT_EQ("?????????", transliterations[transliteration::FULL_KATAKANA]);
  EXPECT_EQ("danka", transliterations[transliteration::HALF_ASCII]);
  EXPECT_EQ("???????????????", transliterations[transliteration::FULL_ASCII]);
  EXPECT_EQ("????????????", transliterations[transliteration::HALF_KATAKANA]);
}

TEST_F(ComposerTest, GetStringFunctions) {
  table_->AddRule("ka", "???", "");
  table_->AddRule("n", "???", "");
  table_->AddRule("na", "???", "");
  table_->AddRule("sa", "???", "");

  // Query: "!kan"
  composer_->InsertCharacter("!kan");
  std::string preedit;
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("?????????", preedit);

  std::string submission;
  composer_->GetStringForSubmission(&submission);
  EXPECT_EQ("?????????", submission);

  std::string conversion;
  composer_->GetQueryForConversion(&conversion);
  EXPECT_EQ("!??????", conversion);

  std::string prediction;
  composer_->GetQueryForPrediction(&prediction);
  EXPECT_EQ("!???", prediction);

  // Query: "kas"
  composer_->EditErase();
  composer_->InsertCharacter("kas");

  preedit.clear();
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("??????", preedit);

  submission.clear();
  composer_->GetStringForSubmission(&submission);
  EXPECT_EQ("??????", submission);

  // Pending chars should remain.  This is a test against
  // http://b/1799399
  conversion.clear();
  composer_->GetQueryForConversion(&conversion);
  EXPECT_EQ("???s", conversion);

  prediction.clear();
  composer_->GetQueryForPrediction(&prediction);
  EXPECT_EQ("???", prediction);

  // Query: "s"
  composer_->EditErase();
  composer_->InsertCharacter("s");

  preedit.clear();
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("???", preedit);

  submission.clear();
  composer_->GetStringForSubmission(&submission);
  EXPECT_EQ("???", submission);

  conversion.clear();
  composer_->GetQueryForConversion(&conversion);
  EXPECT_EQ("s", conversion);

  prediction.clear();
  composer_->GetQueryForPrediction(&prediction);
  EXPECT_EQ("s", prediction);

  // Query: "sk"
  composer_->EditErase();
  composer_->InsertCharacter("sk");

  preedit.clear();
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("??????", preedit);

  submission.clear();
  composer_->GetStringForSubmission(&submission);
  EXPECT_EQ("??????", submission);

  conversion.clear();
  composer_->GetQueryForConversion(&conversion);
  EXPECT_EQ("sk", conversion);

  prediction.clear();
  composer_->GetQueryForPrediction(&prediction);
  EXPECT_EQ("sk", prediction);
}

TEST_F(ComposerTest, GetQueryForPredictionHalfAscii) {
  // Dummy setup of romanji table.
  table_->AddRule("he", "???", "");
  table_->AddRule("ll", "??????", "");
  table_->AddRule("lo", "???", "");

  // Switch to Half-Latin input mode.
  composer_->SetInputMode(transliteration::HALF_ASCII);

  std::string prediction;
  {
    constexpr char kInput[] = "hello";
    composer_->InsertCharacter(kInput);
    composer_->GetQueryForPrediction(&prediction);
    EXPECT_EQ(kInput, prediction);
  }
  prediction.clear();
  composer_->EditErase();
  {
    constexpr char kInput[] = "hello!";
    composer_->InsertCharacter(kInput);
    composer_->GetQueryForPrediction(&prediction);
    EXPECT_EQ(kInput, prediction);
  }
}

TEST_F(ComposerTest, GetQueryForPredictionFullAscii) {
  // Dummy setup of romanji table.
  table_->AddRule("he", "???", "");
  table_->AddRule("ll", "??????", "");
  table_->AddRule("lo", "???", "");

  // Switch to Full-Latin input mode.
  composer_->SetInputMode(transliteration::FULL_ASCII);

  std::string prediction;
  {
    composer_->InsertCharacter("???????????????");
    composer_->GetQueryForPrediction(&prediction);
    EXPECT_EQ("hello", prediction);
  }
  prediction.clear();
  composer_->EditErase();
  {
    composer_->InsertCharacter("??????????????????");
    composer_->GetQueryForPrediction(&prediction);
    EXPECT_EQ("hello!", prediction);
  }
}

TEST_F(ComposerTest, GetQueriesForPredictionRoman) {
  table_->AddRule("u", "???", "");
  table_->AddRule("ss", "???", "s");
  table_->AddRule("sa", "???", "");
  table_->AddRule("si", "???", "");
  table_->AddRule("su", "???", "");
  table_->AddRule("se", "???", "");
  table_->AddRule("so", "???", "");

  {
    std::string base, preedit;
    std::set<std::string> expanded;
    composer_->EditErase();
    composer_->InsertCharacter("us");
    composer_->GetQueriesForPrediction(&base, &expanded);
    composer_->GetStringForPreedit(&preedit);
    EXPECT_EQ("???", base);
    EXPECT_EQ(7, expanded.size());
    // We can't use EXPECT_NE for iterator
    EXPECT_TRUE(expanded.end() != expanded.find("s"));
    EXPECT_TRUE(expanded.end() != expanded.find("???"));
    EXPECT_TRUE(expanded.end() != expanded.find("???"));
    EXPECT_TRUE(expanded.end() != expanded.find("???"));
    EXPECT_TRUE(expanded.end() != expanded.find("???"));
    EXPECT_TRUE(expanded.end() != expanded.find("???"));
    EXPECT_TRUE(expanded.end() != expanded.find("???"));
  }
}

TEST_F(ComposerTest, GetQueriesForPredictionMobile) {
  table_->AddRule("_", "", "???");
  table_->AddRule("???*", "", "???");
  table_->AddRule("???*", "", "???");
  table_->AddRule("$", "", "???");
  table_->AddRule("???*", "", "???");
  table_->AddRule("???*", "", "???");
  table_->AddRule("x", "", "???");
  table_->AddRule("???*", "", "???");
  table_->AddRule("???*", "", "???");
  table_->AddRule("???*", "", "???");

  {
    std::string base, preedit;
    std::set<std::string> expanded;
    composer_->EditErase();
    composer_->InsertCharacter("_$");
    composer_->GetQueriesForPrediction(&base, &expanded);
    composer_->GetStringForPreedit(&preedit);
    EXPECT_EQ("???", base);
    EXPECT_EQ(2, expanded.size());
    EXPECT_TRUE(expanded.end() != expanded.find("???"));
    EXPECT_TRUE(expanded.end() != expanded.find("???"));
  }
  {
    std::string base, preedit;
    std::set<std::string> expanded;
    composer_->EditErase();
    composer_->InsertCharacter("_$*");
    composer_->GetQueriesForPrediction(&base, &expanded);
    composer_->GetStringForPreedit(&preedit);
    EXPECT_EQ("???", base);
    EXPECT_EQ(1, expanded.size());
    EXPECT_TRUE(expanded.end() != expanded.find("???"));
  }
  {
    std::string base, preedit;
    std::set<std::string> expanded;
    composer_->EditErase();
    composer_->InsertCharacter("_x*");
    composer_->GetQueriesForPrediction(&base, &expanded);
    composer_->GetStringForPreedit(&preedit);
    EXPECT_EQ("???", base);
    EXPECT_EQ(1, expanded.size());
    EXPECT_TRUE(expanded.end() != expanded.find("???"));
  }
}

TEST_F(ComposerTest, GetStringFunctionsForN) {
  table_->AddRule("a", "[A]", "");
  table_->AddRule("n", "[N]", "");
  table_->AddRule("nn", "[N]", "");
  table_->AddRule("na", "[NA]", "");
  table_->AddRule("nya", "[NYA]", "");
  table_->AddRule("ya", "[YA]", "");
  table_->AddRule("ka", "[KA]", "");

  composer_->InsertCharacter("nynyan");
  std::string preedit;
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("????????????????????????", preedit);

  std::string submission;
  composer_->GetStringForSubmission(&submission);
  EXPECT_EQ("????????????????????????", submission);

  std::string conversion;
  composer_->GetQueryForConversion(&conversion);
  EXPECT_EQ("ny[NYA][N]", conversion);

  std::string prediction;
  composer_->GetQueryForPrediction(&prediction);
  EXPECT_EQ("ny[NYA]", prediction);

  composer_->InsertCharacter("ka");
  std::string conversion2;
  composer_->GetQueryForConversion(&conversion2);
  EXPECT_EQ("ny[NYA][N][KA]", conversion2);

  std::string prediction2;
  composer_->GetQueryForPrediction(&prediction2);
  EXPECT_EQ("ny[NYA][N][KA]", prediction2);
}

TEST_F(ComposerTest, GetStringFunctionsInputFieldType) {
  const struct TestData {
    const commands::Context::InputFieldType field_type;
    const bool ascii_expected;
  } test_data_list[] = {
      {commands::Context::NORMAL, false},
      {commands::Context::NUMBER, true},
      {commands::Context::PASSWORD, true},
      {commands::Context::TEL, true},
  };

  composer_->SetInputMode(transliteration::HIRAGANA);
  for (size_t test_data_index = 0; test_data_index < std::size(test_data_list);
       ++test_data_index) {
    const TestData &test_data = test_data_list[test_data_index];
    composer_->SetInputFieldType(test_data.field_type);
    std::string key, converted;
    for (char c = 0x20; c <= 0x7E; ++c) {
      key.assign(1, c);
      composer_->EditErase();
      composer_->InsertCharacter(key);
      if (test_data.ascii_expected) {
        composer_->GetStringForPreedit(&converted);
        EXPECT_EQ(key, converted);
        composer_->GetStringForSubmission(&converted);
        EXPECT_EQ(key, converted);
      } else {
        // Expected result is FULL_WIDTH form.
        // Typically the result is a full-width form of the key,
        // but some characters are not.
        // So here we checks only the character form.
        composer_->GetStringForPreedit(&converted);
        EXPECT_EQ(Util::FULL_WIDTH, Util::GetFormType(converted));
        composer_->GetStringForSubmission(&converted);
        EXPECT_EQ(Util::FULL_WIDTH, Util::GetFormType(converted));
      }
    }
  }
}

TEST_F(ComposerTest, InsertCommandCharacter) {
  composer_->SetInputMode(transliteration::HALF_ASCII);

  composer_->InsertCommandCharacter(Composer::REWIND);
  EXPECT_EQ("\x0F<\x0E", GetPreedit(composer_.get()));

  composer_->Reset();
  composer_->InsertCommandCharacter(Composer::STOP_KEY_TOGGLING);
  EXPECT_EQ("\x0F!\x0E", GetPreedit(composer_.get()));
}

TEST_F(ComposerTest, InsertCharacterKeyEvent) {
  commands::KeyEvent key;
  table_->AddRule("a", "???", "");

  key.set_key_code('a');
  composer_->InsertCharacterKeyEvent(key);

  std::string preedit;
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("???", preedit);

  // Half width "A" will be inserted.
  key.set_key_code('A');
  composer_->InsertCharacterKeyEvent(key);

  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("???A", preedit);

  // Half width "a" will be inserted.
  key.set_key_code('a');
  composer_->InsertCharacterKeyEvent(key);

  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("???Aa", preedit);

  // Reset() should revert the previous input mode (Hiragana).
  composer_->Reset();

  key.set_key_code('a');
  composer_->InsertCharacterKeyEvent(key);
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("???", preedit);

  // Typing "A" temporarily switch the input mode.  The input mode
  // should be reverted back after reset.
  composer_->SetInputMode(transliteration::FULL_KATAKANA);
  key.set_key_code('a');
  composer_->InsertCharacterKeyEvent(key);
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("??????", preedit);

  key.set_key_code('A');
  composer_->InsertCharacterKeyEvent(key);
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("??????A", preedit);

  // Reset() should revert the previous input mode (Katakana).
  composer_->Reset();

  key.set_key_code('a');
  composer_->InsertCharacterKeyEvent(key);
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("???", preedit);
}

namespace {
constexpr char kYama[] = "???";
constexpr char kKawa[] = "???";
constexpr char kSora[] = "???";
}  // namespace

TEST_F(ComposerTest, InsertCharacterKeyEventWithUcs4KeyCode) {
  commands::KeyEvent key;

  // Input "???" as key_code.
  key.set_key_code(0x5C71);  // U+5C71 = "???"
  composer_->InsertCharacterKeyEvent(key);

  std::string preedit;
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ(kYama, preedit);

  // Input "???" as key_code which is converted to "???".
  table_->AddRule(kYama, kKawa, "");
  composer_->Reset();
  composer_->InsertCharacterKeyEvent(key);
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ(kKawa, preedit);

  // Input ("???", "???") as (key_code, key_string) which is treated as "???".
  key.set_key_string(kSora);
  composer_->Reset();
  composer_->InsertCharacterKeyEvent(key);
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ(kSora, preedit);
}

TEST_F(ComposerTest, InsertCharacterKeyEventWithoutKeyCode) {
  commands::KeyEvent key;

  // Input "???" as key_string.  key_code is empty.
  key.set_key_string(kYama);
  composer_->InsertCharacterKeyEvent(key);
  EXPECT_FALSE(key.has_key_code());

  std::string preedit;
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ(kYama, preedit);

  transliteration::Transliterations transliterations;
  composer_->GetTransliterations(&transliterations);
  EXPECT_EQ(kYama, transliterations[transliteration::HIRAGANA]);
  EXPECT_EQ(kYama, transliterations[transliteration::HALF_ASCII]);
}

TEST_F(ComposerTest, InsertCharacterKeyEventWithAsIs) {
  commands::KeyEvent key;
  table_->AddRule("a", "???", "");
  table_->AddRule("-", "???", "");

  key.set_key_code('a');
  composer_->InsertCharacterKeyEvent(key);

  std::string preedit;
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("???", preedit);

  // Full width "???" will be inserted.
  key.set_key_code('0');
  key.set_key_string("0");
  composer_->InsertCharacterKeyEvent(key);

  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("??????", preedit);

  // Half width "0" will be inserted.
  key.set_key_code('0');
  key.set_key_string("0");
  key.set_input_style(commands::KeyEvent::AS_IS);
  composer_->InsertCharacterKeyEvent(key);

  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("??????0", preedit);

  // Full width "0" will be inserted.
  key.set_key_code('0');
  key.set_key_string("0");
  key.set_input_style(commands::KeyEvent::FOLLOW_MODE);
  composer_->InsertCharacterKeyEvent(key);

  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("??????0???", preedit);

  // Half width "-" will be inserted.
  key.set_key_code('-');
  key.set_key_string("-");
  key.set_input_style(commands::KeyEvent::AS_IS);
  composer_->InsertCharacterKeyEvent(key);

  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("??????0???-", preedit);

  // Full width "???" (U+2212) will be inserted.
  key.set_key_code('-');
  key.set_key_string("???");
  key.set_input_style(commands::KeyEvent::FOLLOW_MODE);
  composer_->InsertCharacterKeyEvent(key);

  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("??????0???-???", preedit);  // The last hyphen is U+2212.
}

TEST_F(ComposerTest, InsertCharacterKeyEventWithInputMode) {
  table_->AddRule("a", "???", "");
  table_->AddRule("i", "???", "");
  table_->AddRule("u", "???", "");

  {
    // "a" ??? "???" (Hiragana)
    EXPECT_TRUE(InsertKeyWithMode("a", commands::HIRAGANA, composer_.get()));
    EXPECT_EQ("???", GetPreedit(composer_.get()));
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());

    // "aI" ??? "???I" (Alphanumeric)
    EXPECT_TRUE(InsertKeyWithMode("I", commands::HIRAGANA, composer_.get()));
    EXPECT_EQ("???I", GetPreedit(composer_.get()));
    EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

    // "u" ??? "???Iu" (Alphanumeric)
    EXPECT_TRUE(InsertKeyWithMode("u", commands::HALF_ASCII, composer_.get()));
    EXPECT_EQ("???Iu", GetPreedit(composer_.get()));
    EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

    // [shift] ??? "???Iu" (Hiragana)
    EXPECT_TRUE(
        InsertKeyWithMode("Shift", commands::HALF_ASCII, composer_.get()));
    EXPECT_EQ("???Iu", GetPreedit(composer_.get()));
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());

    // "u" ??? "???Iu???" (Hiragana)
    EXPECT_TRUE(InsertKeyWithMode("u", commands::HIRAGANA, composer_.get()));
    EXPECT_EQ("???Iu???", GetPreedit(composer_.get()));
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
  }

  composer_ =
      std::make_unique<Composer>(table_.get(), request_.get(), config_.get());

  {
    // "a" ??? "???" (Hiragana)
    EXPECT_TRUE(InsertKeyWithMode("a", commands::HIRAGANA, composer_.get()));
    EXPECT_EQ("???", GetPreedit(composer_.get()));
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());

    // "i" (Katakana) ??? "??????" (Katakana)
    EXPECT_TRUE(
        InsertKeyWithMode("i", commands::FULL_KATAKANA, composer_.get()));
    EXPECT_EQ("??????", GetPreedit(composer_.get()));
    EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

    // SetInputMode(Alphanumeric) ??? "??????" (Alphanumeric)
    composer_->SetInputMode(transliteration::HALF_ASCII);
    EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

    // [shift] ??? "??????" (Alphanumeric) - Nothing happens.
    EXPECT_TRUE(
        InsertKeyWithMode("Shift", commands::HALF_ASCII, composer_.get()));
    EXPECT_EQ("??????", GetPreedit(composer_.get()));
    EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

    // "U" ??? "??????" (Alphanumeric)
    EXPECT_TRUE(InsertKeyWithMode("U", commands::HALF_ASCII, composer_.get()));
    EXPECT_EQ("??????U", GetPreedit(composer_.get()));
    EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

    // [shift] ??? "??????U" (Alphanumeric) - Nothing happens.
    EXPECT_TRUE(
        InsertKeyWithMode("Shift", commands::HALF_ASCII, composer_.get()));
    EXPECT_EQ("??????U", GetPreedit(composer_.get()));
    EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());
  }
}

TEST_F(ComposerTest, ApplyTemporaryInputMode) {
  constexpr bool kCapsLocked = true;
  constexpr bool kCapsUnlocked = false;

  table_->AddRule("a", "???", "");
  composer_->SetInputMode(transliteration::HIRAGANA);

  // Since handlings of continuous shifted input differ,
  // test cases differ between ASCII_INPUT_MODE and KATAKANA_INPUT_MODE

  {  // ASCII_INPUT_MODE (w/o CapsLock)
    config_->set_shift_key_mode_switch(Config::ASCII_INPUT_MODE);

    // pair<input, use_temporary_input_mode>
    std::pair<std::string, bool> kTestDataAscii[] = {
        std::make_pair("a", false),  std::make_pair("A", true),
        std::make_pair("a", true),   std::make_pair("a", true),
        std::make_pair("A", true),   std::make_pair("A", true),
        std::make_pair("a", false),  std::make_pair("A", true),
        std::make_pair("A", true),   std::make_pair("A", true),
        std::make_pair("a", false),  std::make_pair("A", true),
        std::make_pair(".", true),   std::make_pair("a", true),
        std::make_pair("A", true),   std::make_pair("A", true),
        std::make_pair(".", true),   std::make_pair("a", true),
        std::make_pair("???", false), std::make_pair("a", false),
    };

    for (int i = 0; i < std::size(kTestDataAscii); ++i) {
      composer_->ApplyTemporaryInputMode(kTestDataAscii[i].first,
                                         kCapsUnlocked);

      const transliteration::TransliterationType expected =
          kTestDataAscii[i].second ? transliteration::HALF_ASCII
                                   : transliteration::HIRAGANA;

      EXPECT_EQ(expected, composer_->GetInputMode()) << "index=" << i;
      EXPECT_EQ(transliteration::HIRAGANA, composer_->GetComebackInputMode())
          << "index=" << i;
    }
  }

  {  // ASCII_INPUT_MODE (w/ CapsLock)
    config_->set_shift_key_mode_switch(Config::ASCII_INPUT_MODE);

    // pair<input, use_temporary_input_mode>
    std::pair<std::string, bool> kTestDataAscii[] = {
        std::make_pair("A", false),  std::make_pair("a", true),
        std::make_pair("A", true),   std::make_pair("A", true),
        std::make_pair("a", true),   std::make_pair("a", true),
        std::make_pair("A", false),  std::make_pair("a", true),
        std::make_pair("a", true),   std::make_pair("a", true),
        std::make_pair("A", false),  std::make_pair("a", true),
        std::make_pair(".", true),   std::make_pair("A", true),
        std::make_pair("a", true),   std::make_pair("a", true),
        std::make_pair(".", true),   std::make_pair("A", true),
        std::make_pair("???", false), std::make_pair("A", false),
    };

    for (int i = 0; i < std::size(kTestDataAscii); ++i) {
      composer_->ApplyTemporaryInputMode(kTestDataAscii[i].first, kCapsLocked);

      const transliteration::TransliterationType expected =
          kTestDataAscii[i].second ? transliteration::HALF_ASCII
                                   : transliteration::HIRAGANA;

      EXPECT_EQ(expected, composer_->GetInputMode()) << "index=" << i;
      EXPECT_EQ(transliteration::HIRAGANA, composer_->GetComebackInputMode())
          << "index=" << i;
    }
  }

  {  // KATAKANA_INPUT_MODE (w/o CapsLock)
    config_->set_shift_key_mode_switch(Config::KATAKANA_INPUT_MODE);

    // pair<input, use_temporary_input_mode>
    std::pair<std::string, bool> kTestDataKatakana[] = {
        std::make_pair("a", false),  std::make_pair("A", true),
        std::make_pair("a", false),  std::make_pair("a", false),
        std::make_pair("A", true),   std::make_pair("A", true),
        std::make_pair("a", false),  std::make_pair("A", true),
        std::make_pair("A", true),   std::make_pair("A", true),
        std::make_pair("a", false),  std::make_pair("A", true),
        std::make_pair(".", true),   std::make_pair("a", false),
        std::make_pair("A", true),   std::make_pair("A", true),
        std::make_pair(".", true),   std::make_pair("a", false),
        std::make_pair("???", false), std::make_pair("a", false),
    };

    for (int i = 0; i < std::size(kTestDataKatakana); ++i) {
      composer_->ApplyTemporaryInputMode(kTestDataKatakana[i].first,
                                         kCapsUnlocked);

      const transliteration::TransliterationType expected =
          kTestDataKatakana[i].second ? transliteration::FULL_KATAKANA
                                      : transliteration::HIRAGANA;

      EXPECT_EQ(expected, composer_->GetInputMode()) << "index=" << i;
      EXPECT_EQ(transliteration::HIRAGANA, composer_->GetComebackInputMode())
          << "index=" << i;
    }
  }

  {  // KATAKANA_INPUT_MODE (w/ CapsLock)
    config_->set_shift_key_mode_switch(Config::KATAKANA_INPUT_MODE);

    // pair<input, use_temporary_input_mode>
    std::pair<std::string, bool> kTestDataKatakana[] = {
        std::make_pair("A", false),  std::make_pair("a", true),
        std::make_pair("A", false),  std::make_pair("A", false),
        std::make_pair("a", true),   std::make_pair("a", true),
        std::make_pair("A", false),  std::make_pair("a", true),
        std::make_pair("a", true),   std::make_pair("a", true),
        std::make_pair("A", false),  std::make_pair("a", true),
        std::make_pair(".", true),   std::make_pair("A", false),
        std::make_pair("a", true),   std::make_pair("a", true),
        std::make_pair(".", true),   std::make_pair("A", false),
        std::make_pair("???", false), std::make_pair("A", false),
    };

    for (int i = 0; i < std::size(kTestDataKatakana); ++i) {
      composer_->ApplyTemporaryInputMode(kTestDataKatakana[i].first,
                                         kCapsLocked);

      const transliteration::TransliterationType expected =
          kTestDataKatakana[i].second ? transliteration::FULL_KATAKANA
                                      : transliteration::HIRAGANA;

      EXPECT_EQ(expected, composer_->GetInputMode()) << "index=" << i;
      EXPECT_EQ(transliteration::HIRAGANA, composer_->GetComebackInputMode())
          << "index=" << i;
    }
  }
}

TEST_F(ComposerTest, FullWidthCharRulesb31444698) {
  // Construct the following romaji table:
  //
  // 1<tab><tab>{?}???<tab>NewChunk NoTransliteration
  // {?}???1<tab><tab>{?}???<tab>
  // ???<tab><tab>{?}???<tab>NewChunk NoTransliteration
  // {?}??????<tab><tab>{?}???<tab>
  constexpr int kAttrs =
      TableAttribute::NEW_CHUNK | TableAttribute::NO_TRANSLITERATION;
  table_->AddRuleWithAttributes("1", "", "{?}???", kAttrs);
  table_->AddRule("{?}???1", "", "{?}???");
  table_->AddRuleWithAttributes("???", "", "{?}???", kAttrs);
  table_->AddRule("{?}??????", "", "{?}???");

  // Test if "11" is transliterated to "???"
  ASSERT_TRUE(InsertKeyWithMode("1", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("???", GetPreedit(composer_.get()));
  ASSERT_TRUE(InsertKeyWithMode("1", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("???", GetPreedit(composer_.get()));

  composer_->Reset();

  // b/31444698.  Test if "??????" is transliterated to "???"
  ASSERT_TRUE(InsertKeyWithMode("???", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("???", GetPreedit(composer_.get()));
  ASSERT_TRUE(InsertKeyWithMode("???", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("???", GetPreedit(composer_.get()));
}

TEST_F(ComposerTest, Copy) {
  table_->AddRule("a", "???", "");
  table_->AddRule("n", "???", "");
  table_->AddRule("na", "???", "");

  {
    SCOPED_TRACE("Precomposition");

    std::string src_composition;
    composer_->GetStringForSubmission(&src_composition);
    EXPECT_EQ("", src_composition);

    Composer dest = *composer_;
    ExpectSameComposer(*composer_, dest);
  }

  {
    SCOPED_TRACE("Composition");

    composer_->InsertCharacter("a");
    composer_->InsertCharacter("n");
    std::string src_composition;
    composer_->GetStringForSubmission(&src_composition);
    EXPECT_EQ("??????", src_composition);

    Composer dest = *composer_;
    ExpectSameComposer(*composer_, dest);
  }

  {
    SCOPED_TRACE("Conversion");

    std::string src_composition;
    composer_->GetQueryForConversion(&src_composition);
    EXPECT_EQ("??????", src_composition);

    Composer dest = *composer_;
    ExpectSameComposer(*composer_, dest);
  }

  {
    SCOPED_TRACE("Composition with temporary input mode");

    composer_->Reset();
    InsertKey("A", composer_.get());
    InsertKey("a", composer_.get());
    InsertKey("A", composer_.get());
    InsertKey("A", composer_.get());
    InsertKey("a", composer_.get());
    std::string src_composition;
    composer_->GetStringForSubmission(&src_composition);
    EXPECT_EQ("AaAA???", src_composition);

    Composer dest = *composer_;
    ExpectSameComposer(*composer_, dest);
  }

  {
    SCOPED_TRACE("Composition with password mode");

    composer_->Reset();
    composer_->SetInputFieldType(commands::Context::PASSWORD);
    composer_->SetInputMode(transliteration::HALF_ASCII);
    composer_->SetOutputMode(transliteration::HALF_ASCII);
    composer_->InsertCharacter("M");
    std::string src_composition;
    composer_->GetStringForSubmission(&src_composition);
    EXPECT_EQ("M", src_composition);

    Composer dest = *composer_;
    ExpectSameComposer(*composer_, dest);
  }
}

TEST_F(ComposerTest, ShiftKeyOperation) {
  commands::KeyEvent key;
  table_->AddRule("a", "???", "");

  {  // Basic feature.
    composer_->Reset();
    InsertKey("a", composer_.get());  // "???"
    InsertKey("A", composer_.get());  // "???A"
    InsertKey("a", composer_.get());  // "???Aa"
    // Shift reverts the input mode to Hiragana.
    InsertKey("Shift", composer_.get());
    InsertKey("a", composer_.get());  // "???Aa???"
    // Shift does nothing because the input mode has already been reverted.
    InsertKey("Shift", composer_.get());
    InsertKey("a", composer_.get());  // "???Aa??????"

    std::string preedit;
    composer_->GetStringForPreedit(&preedit);
    EXPECT_EQ("???Aa??????", preedit);
  }

  {  // Revert back to the previous input mode.
    composer_->SetInputMode(transliteration::FULL_KATAKANA);
    composer_->Reset();
    InsertKey("a", composer_.get());  // "???"
    InsertKey("A", composer_.get());  // "???A"
    InsertKey("a", composer_.get());  // "???Aa"
    // Shift reverts the input mode to Hiragana.
    InsertKey("Shift", composer_.get());
    InsertKey("a", composer_.get());  // "???Aa???"
    // Shift does nothing because the input mode has already been reverted.
    InsertKey("Shift", composer_.get());
    InsertKey("a", composer_.get());  // "???Aa??????"

    std::string preedit;
    composer_->GetStringForPreedit(&preedit);
    EXPECT_EQ("???Aa??????", preedit);
    EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());
  }

  {  // Multiple shifted characters
    composer_->SetInputMode(transliteration::HIRAGANA);
    composer_->Reset();
    // Sequential shfited keys change the behavior of the next
    // non-shifted key.  "AAaa" Should become "AA??????", "Aaa" should
    // become "Aaa".
    InsertKey("A", composer_.get());  // "A"
    InsertKey("A", composer_.get());  // "AA"
    InsertKey("a", composer_.get());  // "AA???"
    InsertKey("A", composer_.get());  // "AA???A"
    InsertKey("a", composer_.get());  // "AA???Aa"

    std::string preedit;
    composer_->GetStringForPreedit(&preedit);
    EXPECT_EQ("AA???Aa", preedit);
  }

  {  // Multiple shifted characters #2
    composer_->SetInputMode(transliteration::HIRAGANA);
    composer_->Reset();
    InsertKey("D", composer_.get());  // "D"
    InsertKey("&", composer_.get());  // "D&"
    InsertKey("D", composer_.get());  // "D&D"
    InsertKey("2", composer_.get());  // "D&D2"
    InsertKey("a", composer_.get());  // "D&D2a"

    std::string preedit;
    composer_->GetStringForPreedit(&preedit);
    EXPECT_EQ("D&D2a", preedit);
  }

  {  // Full-witdh alphanumeric
    composer_->SetInputMode(transliteration::FULL_ASCII);
    composer_->Reset();
    InsertKey("A", composer_.get());  // "???"
    InsertKey("a", composer_.get());  // "??????"

    std::string preedit;
    composer_->GetStringForPreedit(&preedit);
    EXPECT_EQ("??????", preedit);
  }

  {  // Half-witdh alphanumeric
    composer_->SetInputMode(transliteration::HALF_ASCII);
    composer_->Reset();
    InsertKey("A", composer_.get());  // "A"
    InsertKey("a", composer_.get());  // "Aa"

    std::string preedit;
    composer_->GetStringForPreedit(&preedit);
    EXPECT_EQ("Aa", preedit);
  }
}

TEST_F(ComposerTest, ShiftKeyOperationForKatakana) {
  config_->set_shift_key_mode_switch(Config::KATAKANA_INPUT_MODE);
  table_->InitializeWithRequestAndConfig(*request_, *config_,
                                         mock_data_manager_);
  composer_->Reset();
  composer_->SetInputMode(transliteration::HIRAGANA);
  InsertKey("K", composer_.get());
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());
  InsertKey("A", composer_.get());
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());
  InsertKey("T", composer_.get());
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());
  InsertKey("a", composer_.get());
  // See the below comment.
  // EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());
  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
  InsertKey("k", composer_.get());
  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
  InsertKey("A", composer_.get());
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());
  InsertKey("n", composer_.get());
  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
  InsertKey("a", composer_.get());
  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());

  std::string preedit;
  composer_->GetStringForPreedit(&preedit);
  // NOTE(komatsu): "KATakAna" is converted to "??????????????????" rather
  // than "????????????".  This is a different behavior from Kotoeri due
  // to avoid complecated implementation.  Unless this is a problem
  // for users, this difference probably remains.
  //
  // EXPECT_EQ("????????????", preedit);

  EXPECT_EQ("??????????????????", preedit);
}

TEST_F(ComposerTest, AutoIMETurnOffEnabled) {
  config_->set_preedit_method(Config::ROMAN);
  config_->set_use_auto_ime_turn_off(true);

  table_->InitializeWithRequestAndConfig(*request_, *config_,
                                         mock_data_manager_);

  commands::KeyEvent key;

  {  // http
    InsertKey("h", composer_.get());
    InsertKey("t", composer_.get());
    InsertKey("t", composer_.get());
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
    InsertKey("p", composer_.get());

    std::string preedit;
    composer_->GetStringForPreedit(&preedit);
    EXPECT_EQ("http", preedit);
    EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

    composer_->Reset();
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
  }

  composer_ =
      std::make_unique<Composer>(table_.get(), request_.get(), config_.get());

  {  // google
    InsertKey("g", composer_.get());
    InsertKey("o", composer_.get());
    InsertKey("o", composer_.get());
    InsertKey("g", composer_.get());
    InsertKey("l", composer_.get());
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
    InsertKey("e", composer_.get());
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
    EXPECT_EQ("google", GetPreedit(composer_.get()));

    InsertKey("a", composer_.get());
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
    EXPECT_EQ("google???", GetPreedit(composer_.get()));

    composer_->Reset();
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
  }

  {  // google in full-width alphanumeric mode.
    composer_->SetInputMode(transliteration::FULL_ASCII);
    InsertKey("g", composer_.get());
    InsertKey("o", composer_.get());
    InsertKey("o", composer_.get());
    InsertKey("g", composer_.get());
    InsertKey("l", composer_.get());
    EXPECT_EQ(transliteration::FULL_ASCII, composer_->GetInputMode());
    InsertKey("e", composer_.get());
    EXPECT_EQ(transliteration::FULL_ASCII, composer_->GetInputMode());

    EXPECT_EQ("??????????????????", GetPreedit(composer_.get()));

    InsertKey("a", composer_.get());
    EXPECT_EQ(transliteration::FULL_ASCII, composer_->GetInputMode());
    EXPECT_EQ("?????????????????????", GetPreedit(composer_.get()));

    composer_->Reset();
    EXPECT_EQ(transliteration::FULL_ASCII, composer_->GetInputMode());
    // Reset to Hiragana mode
    composer_->SetInputMode(transliteration::HIRAGANA);
  }

  {  // Google
    InsertKey("G", composer_.get());
    EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());
    InsertKey("o", composer_.get());
    InsertKey("o", composer_.get());
    InsertKey("g", composer_.get());
    InsertKey("l", composer_.get());
    EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());
    InsertKey("e", composer_.get());
    EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());
    EXPECT_EQ("Google", GetPreedit(composer_.get()));

    InsertKey("a", composer_.get());
    EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());
    EXPECT_EQ("Googlea", GetPreedit(composer_.get()));

    composer_->Reset();
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
  }

  config_->set_shift_key_mode_switch(Config::OFF);
  composer_ =
      std::make_unique<Composer>(table_.get(), request_.get(), config_.get());

  {  // Google
    InsertKey("G", composer_.get());
    InsertKey("o", composer_.get());
    InsertKey("o", composer_.get());
    InsertKey("g", composer_.get());
    InsertKey("l", composer_.get());
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
    InsertKey("e", composer_.get());
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
    EXPECT_EQ("Google", GetPreedit(composer_.get()));

    InsertKey("a", composer_.get());
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
    EXPECT_EQ("Google???", GetPreedit(composer_.get()));

    composer_->Reset();
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
  }
}

TEST_F(ComposerTest, AutoIMETurnOffDisabled) {
  config_->set_preedit_method(Config::ROMAN);
  config_->set_use_auto_ime_turn_off(false);

  table_->InitializeWithRequestAndConfig(*request_, *config_,
                                         mock_data_manager_);

  commands::KeyEvent key;

  // Roman
  key.set_key_code('h');
  composer_->InsertCharacterKeyEvent(key);

  key.set_key_code('t');
  composer_->InsertCharacterKeyEvent(key);

  key.set_key_code('t');
  composer_->InsertCharacterKeyEvent(key);

  key.set_key_code('p');
  composer_->InsertCharacterKeyEvent(key);

  key.set_key_code(':');
  composer_->InsertCharacterKeyEvent(key);

  key.set_key_code('/');
  composer_->InsertCharacterKeyEvent(key);

  key.set_key_code('/');
  composer_->InsertCharacterKeyEvent(key);

  std::string preedit;
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("?????????????????????", preedit);
}

TEST_F(ComposerTest, AutoIMETurnOffKana) {
  config_->set_preedit_method(Config::KANA);
  config_->set_use_auto_ime_turn_off(true);

  table_->InitializeWithRequestAndConfig(*request_, *config_,
                                         mock_data_manager_);

  commands::KeyEvent key;

  // Kana
  key.set_key_code('h');
  composer_->InsertCharacterKeyEvent(key);

  key.set_key_code('t');
  composer_->InsertCharacterKeyEvent(key);

  key.set_key_code('t');
  composer_->InsertCharacterKeyEvent(key);

  key.set_key_code('p');
  composer_->InsertCharacterKeyEvent(key);

  key.set_key_code(':');
  composer_->InsertCharacterKeyEvent(key);

  key.set_key_code('/');
  composer_->InsertCharacterKeyEvent(key);

  key.set_key_code('/');
  composer_->InsertCharacterKeyEvent(key);

  std::string preedit;
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("?????????????????????", preedit);
}

TEST_F(ComposerTest, KanaPrediction) {
  composer_->InsertCharacterKeyAndPreedit("t", "???");
  {
    std::string preedit;
    composer_->GetQueryForPrediction(&preedit);
    EXPECT_EQ("???", preedit);
  }
  composer_->InsertCharacterKeyAndPreedit("\\", "???");
  {
    std::string preedit;
    composer_->GetQueryForPrediction(&preedit);
    EXPECT_EQ("??????", preedit);
  }
  composer_->InsertCharacterKeyAndPreedit(",", "???");
  {
    std::string preedit;
    composer_->GetQueryForPrediction(&preedit);
    EXPECT_EQ("?????????", preedit);
  }
}

TEST_F(ComposerTest, KanaTransliteration) {
  table_->AddRule("??????", "???", "");
  composer_->InsertCharacterKeyAndPreedit("h", "???");
  composer_->InsertCharacterKeyAndPreedit("e", "???");
  composer_->InsertCharacterKeyAndPreedit("l", "???");
  composer_->InsertCharacterKeyAndPreedit("l", "???");
  composer_->InsertCharacterKeyAndPreedit("o", "???");

  std::string preedit;
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("???????????????", preedit);

  transliteration::Transliterations transliterations;
  composer_->GetTransliterations(&transliterations);
  EXPECT_EQ(transliteration::NUM_T13N_TYPES, transliterations.size());
  EXPECT_EQ("hello", transliterations[transliteration::HALF_ASCII]);
}

TEST_F(ComposerTest, SetOutputMode) {
  table_->AddRule("mo", "???", "");
  table_->AddRule("zu", "???", "");

  composer_->InsertCharacter("m");
  composer_->InsertCharacter("o");
  composer_->InsertCharacter("z");
  composer_->InsertCharacter("u");

  std::string output;
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("??????", output);
  EXPECT_EQ(2, composer_->GetCursor());

  composer_->SetOutputMode(transliteration::HALF_ASCII);
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("mozu", output);
  EXPECT_EQ(4, composer_->GetCursor());

  composer_->SetOutputMode(transliteration::HALF_KATAKANA);
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("?????????", output);
  EXPECT_EQ(3, composer_->GetCursor());
}

TEST_F(ComposerTest, UpdateInputMode) {
  table_->AddRule("a", "???", "");
  table_->AddRule("i", "???", "");

  InsertKey("A", composer_.get());
  EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

  InsertKey("I", composer_.get());
  EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

  InsertKey("a", composer_.get());
  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());

  InsertKey("i", composer_.get());
  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());

  composer_->SetInputMode(transliteration::FULL_ASCII);
  InsertKey("a", composer_.get());
  EXPECT_EQ(transliteration::FULL_ASCII, composer_->GetInputMode());

  InsertKey("i", composer_.get());
  EXPECT_EQ(transliteration::FULL_ASCII, composer_->GetInputMode());

  std::string output;
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("AI????????????", output);

  composer_->SetInputMode(transliteration::FULL_KATAKANA);

  // "|AI????????????"
  composer_->MoveCursorToBeginning();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "A|I????????????"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

  // "AI|????????????"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "AI???|?????????"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());

  // "AI??????|??????"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "AI?????????|???"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::FULL_ASCII, composer_->GetInputMode());

  // "AI????????????|"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::FULL_ASCII, composer_->GetInputMode());

  // "AI?????????|???"
  composer_->MoveCursorLeft();
  EXPECT_EQ(transliteration::FULL_ASCII, composer_->GetInputMode());

  // "|AI????????????"
  composer_->MoveCursorToBeginning();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "A|I????????????"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

  // "A|????????????"
  composer_->Delete();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "A???|?????????"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());

  // "A|?????????"
  composer_->Backspace();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "A?????????|"
  composer_->MoveCursorToEnd();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "A??????|???"
  composer_->MoveCursorLeft();
  EXPECT_EQ(transliteration::FULL_ASCII, composer_->GetInputMode());

  // "A?????????|"
  composer_->MoveCursorToEnd();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());
}

TEST_F(ComposerTest, DisabledUpdateInputMode) {
  // Set the flag disable.
  commands::Request request;
  request.set_update_input_mode_from_surrounding_text(false);
  composer_->SetRequest(&request);

  table_->AddRule("a", "???", "");
  table_->AddRule("i", "???", "");

  InsertKey("A", composer_.get());
  EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

  InsertKey("I", composer_.get());
  EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

  InsertKey("a", composer_.get());
  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());

  InsertKey("i", composer_.get());
  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());

  composer_->SetInputMode(transliteration::FULL_ASCII);
  InsertKey("a", composer_.get());
  EXPECT_EQ(transliteration::FULL_ASCII, composer_->GetInputMode());

  InsertKey("i", composer_.get());
  EXPECT_EQ(transliteration::FULL_ASCII, composer_->GetInputMode());

  std::string output;
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("AI????????????", output);

  composer_->SetInputMode(transliteration::FULL_KATAKANA);

  // Use same scenario as above test case, but the result of GetInputMode
  // should be always FULL_KATAKANA regardless of the surrounding text.

  // "|AI????????????"
  composer_->MoveCursorToBeginning();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "A|I????????????"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "AI|????????????"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "AI???|?????????"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "AI??????|??????"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "AI?????????|???"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "AI????????????|"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "AI?????????|???"
  composer_->MoveCursorLeft();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "|AI????????????"
  composer_->MoveCursorToBeginning();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "A|I????????????"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "A|????????????"
  composer_->Delete();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "A???|?????????"
  composer_->MoveCursorRight();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "A|?????????"
  composer_->Backspace();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "A?????????|"
  composer_->MoveCursorToEnd();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "A??????|???"
  composer_->MoveCursorLeft();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  // "A?????????|"
  composer_->MoveCursorToEnd();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());
}

TEST_F(ComposerTest, TransformCharactersForNumbers) {
  std::string query;

  query = "";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "R2D2";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "??????";
  EXPECT_TRUE(Composer::TransformCharactersForNumbers(&query));
  EXPECT_EQ("??????", query);  // The hyphen is U+2212.

  query = "?????????";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "???";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "??????";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "???????????????";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "???";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "??????";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "?????????";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "@";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "???@";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "??????@";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "???";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "??????";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "?????????";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "???????????????";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "???????????????";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "?????????";
  EXPECT_TRUE(Composer::TransformCharactersForNumbers(&query));
  EXPECT_EQ("?????????", query);

  query = "?????????";
  EXPECT_TRUE(Composer::TransformCharactersForNumbers(&query));
  EXPECT_EQ("?????????", query);

  query = "????????????????????????";
  EXPECT_TRUE(Composer::TransformCharactersForNumbers(&query));
  EXPECT_EQ("????????????????????????", query);  // The hyphen is U+2212.

  query = "?????????";
  EXPECT_TRUE(Composer::TransformCharactersForNumbers(&query));
  EXPECT_EQ("?????????", query);  // The hyphen is U+2212.

  query = "?????????????????????";
  EXPECT_TRUE(Composer::TransformCharactersForNumbers(&query));
  EXPECT_EQ("?????????????????????", query);  // The hyphen is U+2212.

  query = "???????????????????????????";
  EXPECT_TRUE(Composer::TransformCharactersForNumbers(&query));
  EXPECT_EQ("???????????????????????????", query);  // The hyphen is U+2212.

  query = "??????????????????????????????";
  EXPECT_TRUE(Composer::TransformCharactersForNumbers(&query));
  EXPECT_EQ("??????????????????????????????", query);  // The hyphen is U+2212.

  query = "?????????";
  EXPECT_TRUE(Composer::TransformCharactersForNumbers(&query));
  EXPECT_EQ("?????????", query);  // The hyphen is U+2212.

  query = "?????????????????????????????????";
  EXPECT_FALSE(Composer::TransformCharactersForNumbers(&query));

  query = "??????????????????????????????????????????????????????????????????";
  EXPECT_TRUE(Composer::TransformCharactersForNumbers(&query));
  EXPECT_EQ("??????????????????????????????????????????????????????????????????", query);

  query = "????????????????????????????????????";
  EXPECT_TRUE(Composer::TransformCharactersForNumbers(&query));
  EXPECT_EQ("????????????????????????????????????", query);

  query = "????????????";
  EXPECT_TRUE(Composer::TransformCharactersForNumbers(&query));
  EXPECT_EQ("????????????", query);

  query = "????????????";
  EXPECT_TRUE(Composer::TransformCharactersForNumbers(&query));
  EXPECT_EQ("????????????", query);
}

TEST_F(ComposerTest, PreeditFormAfterCharacterTransform) {
  CharacterFormManager *manager =
      CharacterFormManager::GetCharacterFormManager();
  table_->AddRule("0", "???", "");
  table_->AddRule("1", "???", "");
  table_->AddRule("2", "???", "");
  table_->AddRule("3", "???", "");
  table_->AddRule("4", "???", "");
  table_->AddRule("5", "???", "");
  table_->AddRule("6", "???", "");
  table_->AddRule("7", "???", "");
  table_->AddRule("8", "???", "");
  table_->AddRule("9", "???", "");
  table_->AddRule("-", "???", "");
  table_->AddRule(",", "???", "");
  table_->AddRule(".", "???", "");

  {
    composer_->Reset();
    manager->SetDefaultRule();
    manager->AddPreeditRule("1", Config::HALF_WIDTH);
    manager->AddPreeditRule(".,", Config::HALF_WIDTH);
    composer_->InsertCharacter("3.14");
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("3.14", result);
  }

  {
    composer_->Reset();
    manager->SetDefaultRule();
    manager->AddPreeditRule("1", Config::FULL_WIDTH);
    manager->AddPreeditRule(".,", Config::HALF_WIDTH);
    composer_->InsertCharacter("3.14");
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("???.??????", result);
  }

  {
    composer_->Reset();
    manager->SetDefaultRule();
    manager->AddPreeditRule("1", Config::HALF_WIDTH);
    manager->AddPreeditRule(".,", Config::FULL_WIDTH);
    composer_->InsertCharacter("3.14");
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("3???14", result);
  }

  {
    composer_->Reset();
    manager->SetDefaultRule();
    manager->AddPreeditRule("1", Config::FULL_WIDTH);
    manager->AddPreeditRule(".,", Config::FULL_WIDTH);
    composer_->InsertCharacter("3.14");
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("????????????", result);
  }
}

TEST_F(ComposerTest, ComposingWithcharactertransform) {
  table_->AddRule("0", "???", "");
  table_->AddRule("1", "???", "");
  table_->AddRule("2", "???", "");
  table_->AddRule("3", "???", "");
  table_->AddRule("4", "???", "");
  table_->AddRule("5", "???", "");
  table_->AddRule("6", "???", "");
  table_->AddRule("7", "???", "");
  table_->AddRule("8", "???", "");
  table_->AddRule("9", "???", "");
  table_->AddRule("-", "???", "");
  table_->AddRule(",", "???", "");
  table_->AddRule(".", "???", "");
  composer_->InsertCharacter("-1,000.5");

  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("????????????????????????", result);  // The hyphen is U+2212.
  }
  {
    std::string result;
    composer_->GetStringForSubmission(&result);
    EXPECT_EQ("????????????????????????", result);  // The hyphen is U+2212.
  }
  {
    std::string result;
    composer_->GetQueryForConversion(&result);
    EXPECT_EQ("-1,000.5", result);
  }
  {
    std::string result;
    composer_->GetQueryForPrediction(&result);
    EXPECT_EQ("-1,000.5", result);
  }
  {
    std::string left, focused, right;
    // Right edge
    composer_->GetPreedit(&left, &focused, &right);
    EXPECT_EQ("????????????????????????", left);  // The hyphen is U+2212.
    EXPECT_TRUE(focused.empty());
    EXPECT_TRUE(right.empty());

    composer_->MoveCursorLeft();
    composer_->GetPreedit(&left, &focused, &right);
    EXPECT_EQ("?????????????????????", left);  // The hyphen is U+2212.
    EXPECT_EQ("???", focused);
    EXPECT_TRUE(right.empty());

    composer_->MoveCursorLeft();
    composer_->GetPreedit(&left, &focused, &right);
    EXPECT_EQ("??????????????????", left);  // The hyphen is U+2212.
    EXPECT_EQ("???", focused);
    EXPECT_EQ("???", right);

    composer_->MoveCursorLeft();
    composer_->GetPreedit(&left, &focused, &right);
    EXPECT_EQ("???????????????", left);  // The hyphen is U+2212.
    EXPECT_EQ("???", focused);
    EXPECT_EQ("??????", right);

    composer_->MoveCursorLeft();
    composer_->GetPreedit(&left, &focused, &right);
    EXPECT_EQ("????????????", left);  // The hyphen is U+2212.
    EXPECT_EQ("???", focused);
    EXPECT_EQ("?????????", right);

    composer_->MoveCursorLeft();
    composer_->GetPreedit(&left, &focused, &right);
    EXPECT_EQ("?????????", left);  // The hyphen is U+2212.
    EXPECT_EQ("???", focused);
    EXPECT_EQ("????????????", right);

    composer_->MoveCursorLeft();
    composer_->GetPreedit(&left, &focused, &right);
    EXPECT_EQ("??????", left);
    EXPECT_EQ("???", focused);
    EXPECT_EQ("???????????????", right);

    composer_->MoveCursorLeft();
    composer_->GetPreedit(&left, &focused, &right);
    EXPECT_EQ("???", left);  // U+2212.
    EXPECT_EQ("???", focused);
    EXPECT_EQ("??????????????????", right);

    // Left edge
    composer_->MoveCursorLeft();
    composer_->GetPreedit(&left, &focused, &right);
    EXPECT_TRUE(left.empty());
    EXPECT_EQ("???", focused);  // U+2212.
    EXPECT_EQ("?????????????????????", right);
  }
}

TEST_F(ComposerTest, AlphanumericOfSSH) {
  // This is a unittest against http://b/3199626
  // 'ssh' (?????????) + F10 should be 'ssh'.
  table_->AddRule("ss", "[X]", "s");
  table_->AddRule("sha", "[SHA]", "");
  composer_->InsertCharacter("ssh");
  EXPECT_EQ("???????????????", GetPreedit(composer_.get()));

  std::string query;
  composer_->GetQueryForConversion(&query);
  EXPECT_EQ("[X]sh", query);

  transliteration::Transliterations t13ns;
  composer_->GetTransliterations(&t13ns);
  EXPECT_EQ("ssh", t13ns[transliteration::HALF_ASCII]);
}

TEST_F(ComposerTest, Issue2190364) {
  // This is a unittest against http://b/2190364
  commands::KeyEvent key;
  key.set_key_code('a');
  key.set_key_string("???");

  // Toggle the input mode to HALF_ASCII
  composer_->ToggleInputMode();
  EXPECT_TRUE(composer_->InsertCharacterKeyEvent(key));
  std::string output;
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("a", output);

  // Insertion of a space and backspace it should not change the composition.
  composer_->InsertCharacter(" ");
  output.clear();
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("a ", output);

  composer_->Backspace();
  output.clear();
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("a", output);

  // Toggle the input mode to HIRAGANA, the preedit should not be changed.
  composer_->ToggleInputMode();
  output.clear();
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("a", output);

  // "a" should be converted to "???" on Hiragana input mode.
  EXPECT_TRUE(composer_->InsertCharacterKeyEvent(key));
  output.clear();
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("a???", output);
}

TEST_F(ComposerTest, Issue1817410) {
  // This is a unittest against http://b/2190364
  table_->AddRule("ss", "???", "s");

  InsertKey("s", composer_.get());
  InsertKey("s", composer_.get());

  std::string preedit;
  composer_->GetStringForPreedit(&preedit);
  EXPECT_EQ("??????", preedit);

  std::string t13n;
  composer_->GetSubTransliteration(transliteration::HALF_ASCII, 0, 2, &t13n);
  EXPECT_EQ("ss", t13n);

  t13n.clear();
  composer_->GetSubTransliteration(transliteration::HALF_ASCII, 0, 1, &t13n);
  EXPECT_EQ("s", t13n);

  t13n.clear();
  composer_->GetSubTransliteration(transliteration::HALF_ASCII, 1, 1, &t13n);
  EXPECT_EQ("s", t13n);
}

TEST_F(ComposerTest, Issue2272745) {
  // This is a unittest against http://b/2272745.
  // A temporary input mode remains when a composition is canceled.
  {
    InsertKey("G", composer_.get());
    EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

    composer_->Backspace();
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
  }
  composer_->Reset();
  {
    InsertKey("G", composer_.get());
    EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

    composer_->EditErase();
    EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());
  }
}

TEST_F(ComposerTest, Isue2555503) {
  // This is a unittest against http://b/2555503.
  // Mode respects the previous character too much.
  InsertKey("a", composer_.get());
  composer_->SetInputMode(transliteration::FULL_KATAKANA);
  InsertKey("i", composer_.get());
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());

  composer_->Backspace();
  EXPECT_EQ(transliteration::FULL_KATAKANA, composer_->GetInputMode());
}

TEST_F(ComposerTest, Issue2819580Case1) {
  // This is a unittest against http://b/2819580.
  // 'y' after 'n' disappears.
  table_->AddRule("n", "???", "");
  table_->AddRule("na", "???", "");
  table_->AddRule("ya", "???", "");
  table_->AddRule("nya", "??????", "");

  InsertKey("n", composer_.get());
  InsertKey("y", composer_.get());

  std::string result;
  composer_->GetQueryForConversion(&result);
  EXPECT_EQ("???y", result);
}

TEST_F(ComposerTest, Issue2819580Case2) {
  // This is a unittest against http://b/2819580.
  // 'y' after 'n' disappears.
  table_->AddRule("po", "???", "");
  table_->AddRule("n", "???", "");
  table_->AddRule("na", "???", "");
  table_->AddRule("ya", "???", "");
  table_->AddRule("nya", "??????", "");

  InsertKey("p", composer_.get());
  InsertKey("o", composer_.get());
  InsertKey("n", composer_.get());
  InsertKey("y", composer_.get());

  std::string result;
  composer_->GetQueryForConversion(&result);
  EXPECT_EQ("??????y", result);
}

TEST_F(ComposerTest, Issue2819580Case3) {
  // This is a unittest against http://b/2819580.
  // 'y' after 'n' disappears.
  table_->AddRule("n", "???", "");
  table_->AddRule("na", "???", "");
  table_->AddRule("ya", "???", "");
  table_->AddRule("nya", "??????", "");

  InsertKey("z", composer_.get());
  InsertKey("n", composer_.get());
  InsertKey("y", composer_.get());

  std::string result;
  composer_->GetQueryForConversion(&result);
  EXPECT_EQ("z???y", result);
}

TEST_F(ComposerTest, Issue2797991Case1) {
  // This is a unittest against http://b/2797991.
  // Half-width alphanumeric mode quits after [CAPITAL LETTER]:[CAPITAL LETTER]
  // e.g. C:\Wi -> C:\W???

  table_->AddRule("i", "???", "");

  InsertKey("C", composer_.get());
  InsertKey(":", composer_.get());
  InsertKey("\\", composer_.get());
  InsertKey("W", composer_.get());
  InsertKey("i", composer_.get());

  std::string result;
  composer_->GetStringForPreedit(&result);
  EXPECT_EQ("C:\\Wi", result);
}

TEST_F(ComposerTest, Issue2797991Case2) {
  // This is a unittest against http://b/2797991.
  // Half-width alphanumeric mode quits after [CAPITAL LETTER]:[CAPITAL LETTER]
  // e.g. C:\Wi -> C:\W???

  table_->AddRule("i", "???", "");

  InsertKey("C", composer_.get());
  InsertKey(":", composer_.get());
  InsertKey("W", composer_.get());
  InsertKey("i", composer_.get());

  std::string result;
  composer_->GetStringForPreedit(&result);
  EXPECT_EQ("C:Wi", result);
}

TEST_F(ComposerTest, Issue2797991Case3) {
  // This is a unittest against http://b/2797991.
  // Half-width alphanumeric mode quits after [CAPITAL LETTER]:[CAPITAL LETTER]
  // e.g. C:\Wi -> C:\W???

  table_->AddRule("i", "???", "");

  InsertKey("C", composer_.get());
  InsertKey(":", composer_.get());
  InsertKey("\\", composer_.get());
  InsertKey("W", composer_.get());
  InsertKey("i", composer_.get());
  InsertKeyWithMode("i", commands::HIRAGANA, composer_.get());
  std::string result;
  composer_->GetStringForPreedit(&result);
  EXPECT_EQ("C:\\Wi???", result);
}

TEST_F(ComposerTest, Issue2797991Case4) {
  // This is a unittest against http://b/2797991.
  // Half-width alphanumeric mode quits after [CAPITAL LETTER]:[CAPITAL LETTER]
  // e.g. C:\Wi -> C:\W???

  table_->AddRule("i", "???", "");

  InsertKey("c", composer_.get());
  InsertKey(":", composer_.get());
  InsertKey("\\", composer_.get());
  InsertKey("W", composer_.get());
  InsertKey("i", composer_.get());

  std::string result;
  composer_->GetStringForPreedit(&result);
  EXPECT_EQ("c:\\Wi", result);
}

TEST_F(ComposerTest, CaseSensitiveByConfiguration) {
  {
    config_->set_shift_key_mode_switch(Config::OFF);
    table_->InitializeWithRequestAndConfig(*request_, *config_,
                                           mock_data_manager_);

    table_->AddRule("i", "???", "");
    table_->AddRule("I", "???", "");

    InsertKey("i", composer_.get());
    InsertKey("I", composer_.get());
    InsertKey("i", composer_.get());
    InsertKey("I", composer_.get());
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("????????????", result);
  }
  composer_->Reset();
  {
    config_->set_shift_key_mode_switch(Config::ASCII_INPUT_MODE);
    table_->InitializeWithRequestAndConfig(*request_, *config_,
                                           mock_data_manager_);

    table_->AddRule("i", "???", "");
    table_->AddRule("I", "???", "");

    InsertKey("i", composer_.get());
    InsertKey("I", composer_.get());
    InsertKey("i", composer_.get());
    InsertKey("I", composer_.get());
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("???IiI", result);
  }
}

TEST_F(ComposerTest,
       InputUppercaseInAlphanumericModeWithShiftKeyModeSwitchIsKatakana) {
  {
    config_->set_shift_key_mode_switch(Config::KATAKANA_INPUT_MODE);
    table_->InitializeWithRequestAndConfig(*request_, *config_,
                                           mock_data_manager_);

    table_->AddRule("i", "???", "");
    table_->AddRule("I", "???", "");

    {
      composer_->Reset();
      composer_->SetInputMode(transliteration::FULL_ASCII);
      InsertKey("I", composer_.get());
      std::string result;
      composer_->GetStringForPreedit(&result);
      EXPECT_EQ("???", result);
    }

    {
      composer_->Reset();
      composer_->SetInputMode(transliteration::HALF_ASCII);
      InsertKey("I", composer_.get());
      std::string result;
      composer_->GetStringForPreedit(&result);
      EXPECT_EQ("I", result);
    }

    {
      composer_->Reset();
      composer_->SetInputMode(transliteration::FULL_KATAKANA);
      InsertKey("I", composer_.get());
      std::string result;
      composer_->GetStringForPreedit(&result);
      EXPECT_EQ("???", result);
    }

    {
      composer_->Reset();
      composer_->SetInputMode(transliteration::HALF_KATAKANA);
      InsertKey("I", composer_.get());
      std::string result;
      composer_->GetStringForPreedit(&result);
      EXPECT_EQ("???", result);
    }

    {
      composer_->Reset();
      composer_->SetInputMode(transliteration::HIRAGANA);
      InsertKey("I", composer_.get());
      std::string result;
      composer_->GetStringForPreedit(&result);
      EXPECT_EQ("???", result);
    }
  }
}

TEST_F(ComposerTest, DeletingAlphanumericPartShouldQuitToggleAlphanumericMode) {
  // http://b/2206560
  // 1. Type "iGoogle" (preedit text turns to be "???Google")
  // 2. Type Back-space 6 times ("???")
  // 3. Type "i" (should be "??????")

  table_->InitializeWithRequestAndConfig(*request_, *config_,
                                         mock_data_manager_);

  table_->AddRule("i", "???", "");

  InsertKey("i", composer_.get());
  InsertKey("G", composer_.get());
  InsertKey("o", composer_.get());
  InsertKey("o", composer_.get());
  InsertKey("g", composer_.get());
  InsertKey("l", composer_.get());
  InsertKey("e", composer_.get());

  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("???Google", result);
  }

  composer_->Backspace();
  composer_->Backspace();
  composer_->Backspace();
  composer_->Backspace();
  composer_->Backspace();
  composer_->Backspace();

  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("???", result);
  }

  InsertKey("i", composer_.get());

  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("??????", result);
  }
}

TEST_F(ComposerTest, InputModesChangeWhenCursorMoves) {
  // The expectation of this test is the same as MS-IME's

  table_->InitializeWithRequestAndConfig(*request_, *config_,
                                         mock_data_manager_);

  table_->AddRule("i", "???", "");
  table_->AddRule("gi", "???", "");

  InsertKey("i", composer_.get());
  composer_->MoveCursorRight();
  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("???", result);
  }

  composer_->MoveCursorLeft();
  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("???", result);
  }

  InsertKey("G", composer_.get());
  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("G???", result);
  }

  composer_->MoveCursorRight();
  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("G???", result);
  }

  InsertKey("G", composer_.get());
  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("G???G", result);
  }

  composer_->MoveCursorLeft();
  InsertKey("i", composer_.get());
  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("G??????G", result);
  }

  composer_->MoveCursorRight();
  InsertKey("i", composer_.get());
  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("G??????Gi", result);
  }

  InsertKey("G", composer_.get());
  InsertKey("i", composer_.get());
  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("G??????GiGi", result);
  }

  composer_->Backspace();
  composer_->Backspace();
  composer_->Backspace();
  InsertKey("i", composer_.get());
  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("G??????Gi", result);
  }

  InsertKey("G", composer_.get());
  InsertKey("G", composer_.get());
  composer_->MoveCursorRight();
  InsertKey("i", composer_.get());
  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("G??????GiGGi", result);
  }

  composer_->Backspace();
  composer_->Backspace();
  composer_->Backspace();
  composer_->Backspace();
  composer_->Backspace();
  composer_->Backspace();
  composer_->Backspace();
  InsertKey("i", composer_.get());
  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("G???", result);
  }

  composer_->Backspace();
  composer_->MoveCursorLeft();
  composer_->MoveCursorRight();
  InsertKey("i", composer_.get());
  {
    std::string result;
    composer_->GetStringForPreedit(&result);
    EXPECT_EQ("Gi", result);
  }
}

TEST_F(ComposerTest, ShouldCommit) {
  table_->AddRuleWithAttributes("ka", "[KA]", "", DIRECT_INPUT);
  table_->AddRuleWithAttributes("tt", "[X]", "t", DIRECT_INPUT);
  table_->AddRuleWithAttributes("ta", "[TA]", "", NO_TABLE_ATTRIBUTE);

  composer_->InsertCharacter("k");
  EXPECT_FALSE(composer_->ShouldCommit());

  composer_->InsertCharacter("a");
  EXPECT_TRUE(composer_->ShouldCommit());

  composer_->InsertCharacter("t");
  EXPECT_FALSE(composer_->ShouldCommit());

  composer_->InsertCharacter("t");
  EXPECT_FALSE(composer_->ShouldCommit());

  composer_->InsertCharacter("a");
  EXPECT_TRUE(composer_->ShouldCommit());

  composer_->InsertCharacter("t");
  EXPECT_FALSE(composer_->ShouldCommit());

  composer_->InsertCharacter("a");
  EXPECT_FALSE(composer_->ShouldCommit());
}

TEST_F(ComposerTest, ShouldCommitHead) {
  struct TestData {
    const std::string input_text;
    const commands::Context::InputFieldType field_type;
    const bool expected_return;
    const size_t expected_commit_length;
    TestData(const std::string &input_text,
             commands::Context::InputFieldType field_type, bool expected_return,
             size_t expected_commit_length)
        : input_text(input_text),
          field_type(field_type),
          expected_return(expected_return),
          expected_commit_length(expected_commit_length) {}
  };
  const TestData test_data_list[] = {
      // On NORMAL, never commit the head.
      TestData("", commands::Context::NORMAL, false, 0),
      TestData("A", commands::Context::NORMAL, false, 0),
      TestData("AB", commands::Context::NORMAL, false, 0),
      TestData("", commands::Context::PASSWORD, false, 0),
      // On PASSWORD, commit (length - 1) characters.
      TestData("A", commands::Context::PASSWORD, false, 0),
      TestData("AB", commands::Context::PASSWORD, true, 1),
      TestData("ABCDEFGHI", commands::Context::PASSWORD, true, 8),
      // On NUMBER and TEL, commit (length) characters.
      TestData("", commands::Context::NUMBER, false, 0),
      TestData("A", commands::Context::NUMBER, true, 1),
      TestData("AB", commands::Context::NUMBER, true, 2),
      TestData("ABCDEFGHI", commands::Context::NUMBER, true, 9),
      TestData("", commands::Context::TEL, false, 0),
      TestData("A", commands::Context::TEL, true, 1),
      TestData("AB", commands::Context::TEL, true, 2),
      TestData("ABCDEFGHI", commands::Context::TEL, true, 9),
  };

  for (size_t i = 0; i < std::size(test_data_list); ++i) {
    const TestData &test_data = test_data_list[i];
    SCOPED_TRACE(test_data.input_text);
    SCOPED_TRACE(test_data.field_type);
    SCOPED_TRACE(test_data.expected_return);
    SCOPED_TRACE(test_data.expected_commit_length);
    composer_->Reset();
    composer_->SetInputFieldType(test_data.field_type);
    composer_->InsertCharacter(test_data.input_text);
    size_t length_to_commit;
    const bool result = composer_->ShouldCommitHead(&length_to_commit);
    if (test_data.expected_return) {
      EXPECT_TRUE(result);
      EXPECT_EQ(test_data.expected_commit_length, length_to_commit);
    } else {
      EXPECT_FALSE(result);
    }
  }
}

TEST_F(ComposerTest, CursorMovements) {
  composer_->InsertCharacter("mozuku");
  EXPECT_EQ(6, composer_->GetLength());
  EXPECT_EQ(6, composer_->GetCursor());

  composer_->MoveCursorRight();
  EXPECT_EQ(6, composer_->GetCursor());
  composer_->MoveCursorLeft();
  EXPECT_EQ(5, composer_->GetCursor());

  composer_->MoveCursorToBeginning();
  EXPECT_EQ(0, composer_->GetCursor());
  composer_->MoveCursorLeft();
  EXPECT_EQ(0, composer_->GetCursor());
  composer_->MoveCursorRight();
  EXPECT_EQ(1, composer_->GetCursor());

  composer_->MoveCursorTo(0);
  EXPECT_EQ(0, composer_->GetCursor());
  composer_->MoveCursorTo(6);
  EXPECT_EQ(6, composer_->GetCursor());
  composer_->MoveCursorTo(3);
  EXPECT_EQ(3, composer_->GetCursor());
  composer_->MoveCursorTo(10);
  EXPECT_EQ(3, composer_->GetCursor());
  composer_->MoveCursorTo(-1);
  EXPECT_EQ(3, composer_->GetCursor());
}

TEST_F(ComposerTest, SourceText) {
  composer_->SetInputMode(transliteration::HALF_ASCII);
  composer_->InsertCharacterPreedit("mozc");
  composer_->mutable_source_text()->assign("MOZC");
  EXPECT_FALSE(composer_->Empty());
  EXPECT_EQ("mozc", GetPreedit(composer_.get()));
  EXPECT_EQ("MOZC", composer_->source_text());

  composer_->Backspace();
  composer_->Backspace();
  EXPECT_FALSE(composer_->Empty());
  EXPECT_EQ("mo", GetPreedit(composer_.get()));
  EXPECT_EQ("MOZC", composer_->source_text());

  composer_->Reset();
  EXPECT_TRUE(composer_->Empty());
  EXPECT_TRUE(composer_->source_text().empty());
}

TEST_F(ComposerTest, DeleteAt) {
  table_->AddRule("mo", "???", "");
  table_->AddRule("zu", "???", "");

  composer_->InsertCharacter("z");
  EXPECT_EQ("???", GetPreedit(composer_.get()));
  EXPECT_EQ(1, composer_->GetCursor());
  composer_->DeleteAt(0);
  EXPECT_EQ("", GetPreedit(composer_.get()));
  EXPECT_EQ(0, composer_->GetCursor());

  composer_->InsertCharacter("mmoz");
  EXPECT_EQ("?????????", GetPreedit(composer_.get()));
  EXPECT_EQ(3, composer_->GetCursor());
  composer_->DeleteAt(0);
  EXPECT_EQ("??????", GetPreedit(composer_.get()));
  EXPECT_EQ(2, composer_->GetCursor());
  composer_->InsertCharacter("u");
  EXPECT_EQ("??????", GetPreedit(composer_.get()));
  EXPECT_EQ(2, composer_->GetCursor());

  composer_->InsertCharacter("m");
  EXPECT_EQ("?????????", GetPreedit(composer_.get()));
  EXPECT_EQ(3, composer_->GetCursor());
  composer_->DeleteAt(1);
  EXPECT_EQ("??????", GetPreedit(composer_.get()));
  EXPECT_EQ(2, composer_->GetCursor());
  composer_->InsertCharacter("o");
  EXPECT_EQ("??????", GetPreedit(composer_.get()));
  EXPECT_EQ(2, composer_->GetCursor());
}

TEST_F(ComposerTest, DeleteRange) {
  table_->AddRule("mo", "???", "");
  table_->AddRule("zu", "???", "");

  composer_->InsertCharacter("z");
  EXPECT_EQ("???", GetPreedit(composer_.get()));
  EXPECT_EQ(1, composer_->GetCursor());

  composer_->DeleteRange(0, 1);
  EXPECT_EQ("", GetPreedit(composer_.get()));
  EXPECT_EQ(0, composer_->GetCursor());

  composer_->InsertCharacter("mmozmoz");
  EXPECT_EQ("???????????????", GetPreedit(composer_.get()));
  EXPECT_EQ(5, composer_->GetCursor());

  composer_->DeleteRange(0, 3);
  EXPECT_EQ("??????", GetPreedit(composer_.get()));
  EXPECT_EQ(2, composer_->GetCursor());

  composer_->InsertCharacter("u");
  EXPECT_EQ("??????", GetPreedit(composer_.get()));
  EXPECT_EQ(2, composer_->GetCursor());

  composer_->InsertCharacter("xyz");
  composer_->MoveCursorToBeginning();
  composer_->InsertCharacter("mom");
  EXPECT_EQ("?????????????????????", GetPreedit(composer_.get()));
  EXPECT_EQ(2, composer_->GetCursor());

  composer_->DeleteRange(2, 3);
  // "??????|??????"
  EXPECT_EQ("????????????", GetPreedit(composer_.get()));
  EXPECT_EQ(2, composer_->GetCursor());

  composer_->InsertCharacter("o");
  // "??????|??????"
  EXPECT_EQ("????????????", GetPreedit(composer_.get()));
  EXPECT_EQ(2, composer_->GetCursor());

  composer_->DeleteRange(2, 1000);
  // "??????|"
  EXPECT_EQ("??????", GetPreedit(composer_.get()));
  EXPECT_EQ(2, composer_->GetCursor());
}

TEST_F(ComposerTest, 12KeysAsciiGetQueryForPrediction) {
  // http://b/5509480
  commands::Request request;
  request.set_zero_query_suggestion(true);
  request.set_mixed_conversion(true);
  request.set_special_romanji_table(
      commands::Request::TWELVE_KEYS_TO_HALFWIDTHASCII);
  composer_->SetRequest(&request);
  table_->InitializeWithRequestAndConfig(
      request, config::ConfigHandler::DefaultConfig(), mock_data_manager_);
  composer_->InsertCharacter("2");
  EXPECT_EQ("a", GetPreedit(composer_.get()));
  std::string result;
  composer_->GetQueryForConversion(&result);
  EXPECT_EQ("a", result);
  result.clear();
  composer_->GetQueryForPrediction(&result);
  EXPECT_EQ("a", result);
}

TEST_F(ComposerTest, InsertCharacterPreedit) {
  constexpr char kTestStr[] = "??????a???ka???";

  {
    std::string preedit;
    std::string conversion_query;
    std::string prediction_query;
    std::string base;
    std::set<std::string> expanded;
    composer_->InsertCharacterPreedit(kTestStr);
    composer_->GetStringForPreedit(&preedit);
    composer_->GetQueryForConversion(&conversion_query);
    composer_->GetQueryForPrediction(&prediction_query);
    composer_->GetQueriesForPrediction(&base, &expanded);
    EXPECT_FALSE(preedit.empty());
    EXPECT_FALSE(conversion_query.empty());
    EXPECT_FALSE(prediction_query.empty());
    EXPECT_FALSE(base.empty());
  }
  composer_->Reset();
  {
    std::string preedit;
    std::string conversion_query;
    std::string prediction_query;
    std::string base;
    std::set<std::string> expanded;
    std::vector<std::string> chars;
    Util::SplitStringToUtf8Chars(kTestStr, &chars);
    for (size_t i = 0; i < chars.size(); ++i) {
      composer_->InsertCharacterPreedit(chars[i]);
    }
    composer_->GetStringForPreedit(&preedit);
    composer_->GetQueryForConversion(&conversion_query);
    composer_->GetQueryForPrediction(&prediction_query);
    composer_->GetQueriesForPrediction(&base, &expanded);
    EXPECT_FALSE(preedit.empty());
    EXPECT_FALSE(conversion_query.empty());
    EXPECT_FALSE(prediction_query.empty());
    EXPECT_FALSE(base.empty());
  }
}

namespace {
ProbableKeyEvents GetStubProbableKeyEvent(int key_code, double probability) {
  ProbableKeyEvents result;
  ProbableKeyEvent *event;
  event = result.Add();
  event->set_key_code(key_code);
  event->set_probability(probability);
  event = result.Add();
  event->set_key_code('z');
  event->set_probability(1.0f - probability);
  return result;
}

KeyEvent GetKeyEvent(const std::string &raw,
                     ProbableKeyEvents probable_key_events) {
  KeyEvent key_event;
  key_event.set_key_code(Util::Utf8ToUcs4(raw));
  *(key_event.mutable_probable_key_event()) = probable_key_events;
  return key_event;
}

}  // namespace

class MockTypingModel : public TypingModel {
 public:
  MockTypingModel() : TypingModel(nullptr, 0, nullptr, 0, nullptr) {}
  ~MockTypingModel() override = default;
  int GetCost(absl::string_view key) const override { return 10; }
};

// Test fixture for setting up mobile qwerty romaji table to test typing
// corrector inside composer.
class TypingCorrectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_ = std::make_unique<Config>();
    ConfigHandler::GetDefaultConfig(config_.get());
    config_->set_use_typing_correction(true);

    table_ = std::make_unique<Table>();
    table_->LoadFromFile("system://qwerty_mobile-hiragana.tsv");

    request_ = std::make_unique<Request>();
    request_->set_special_romanji_table(Request::QWERTY_MOBILE_TO_HIRAGANA);

    composer_ =
        std::make_unique<Composer>(table_.get(), request_.get(), config_.get());

    table_->typing_model_ = std::make_unique<MockTypingModel>();
  }

  static bool IsTypingCorrectorClearedOrInvalidated(const Composer &composer) {
    std::vector<TypeCorrectedQuery> queries;
    composer.GetTypeCorrectedQueriesForPrediction(&queries);
    return queries.empty();
  }

  std::unique_ptr<Config> config_;
  std::unique_ptr<Request> request_;
  std::unique_ptr<Composer> composer_;
  std::unique_ptr<Table> table_;
};

TEST_F(TypingCorrectionTest, ResetAfterComposerReset) {
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f)));
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  composer_->Reset();
  EXPECT_TRUE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f)));
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
}

TEST_F(TypingCorrectionTest, InvalidateAfterDeleteAt) {
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f)));
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  composer_->DeleteAt(0);
  EXPECT_TRUE(IsTypingCorrectorClearedOrInvalidated(*composer_));
}

TEST_F(TypingCorrectionTest, InvalidateAfterDelete) {
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f)));
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  composer_->Delete();
  EXPECT_TRUE(IsTypingCorrectorClearedOrInvalidated(*composer_));
}

TEST_F(TypingCorrectionTest, InvalidateAfterDeleteRange) {
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f)));
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  composer_->DeleteRange(0, 1);
  EXPECT_TRUE(IsTypingCorrectorClearedOrInvalidated(*composer_));
}

TEST_F(TypingCorrectionTest, InvalidateAfterAsIsKeyEvent) {
  table_->AddRule("a", "???", "");
  commands::KeyEvent key = GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f));
  key.set_key_string("???");
  composer_->InsertCharacterKeyEvent(key);
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));

  key.set_input_style(commands::KeyEvent::AS_IS);
  composer_->InsertCharacterKeyEvent(key);
  EXPECT_TRUE(IsTypingCorrectorClearedOrInvalidated(*composer_));
}

TEST_F(TypingCorrectionTest, ResetAfterEditErase) {
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f)));
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  composer_->EditErase();
  EXPECT_TRUE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f)));
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
}

TEST_F(TypingCorrectionTest, InvalidateAfterBackspace) {
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f)));
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  composer_->Backspace();
  EXPECT_TRUE(IsTypingCorrectorClearedOrInvalidated(*composer_));
}

TEST_F(TypingCorrectionTest, InvalidateAfterMoveCursorLeft) {
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f)));
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  composer_->MoveCursorLeft();
  EXPECT_TRUE(IsTypingCorrectorClearedOrInvalidated(*composer_));
}

TEST_F(TypingCorrectionTest, InvalidateAfterMoveCursorRight) {
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f)));
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  composer_->MoveCursorRight();
  EXPECT_TRUE(IsTypingCorrectorClearedOrInvalidated(*composer_));
}

TEST_F(TypingCorrectionTest, InvalidateAfterMoveCursorToBeginning) {
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f)));
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  composer_->MoveCursorToBeginning();
  EXPECT_TRUE(IsTypingCorrectorClearedOrInvalidated(*composer_));
}

TEST_F(TypingCorrectionTest, InvalidateAfterMoveCursorToEnd) {
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f)));
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  composer_->MoveCursorToEnd();
  EXPECT_TRUE(IsTypingCorrectorClearedOrInvalidated(*composer_));
}

TEST_F(TypingCorrectionTest, InvalidateAfterMoveCursorTo) {
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("a", GetStubProbableKeyEvent('a', 0.9f)));
  composer_->InsertCharacterKeyEvent(
      GetKeyEvent("b", GetStubProbableKeyEvent('a', 0.9f)));
  EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  composer_->MoveCursorTo(0);
  EXPECT_TRUE(IsTypingCorrectorClearedOrInvalidated(*composer_));
}

TEST_F(TypingCorrectionTest, GetTypeCorrectedQueriesForPrediction) {
  // This test only checks if typing correction candidates are nonempty after
  // each key insertion. The quality of typing correction depends on data model
  // and is tested in composer/internal/typing_corrector_test.cc.
  const char *kKeys[] = {"m", "o", "z", "u", "k", "u"};
  for (size_t i = 0; i < std::size(kKeys); ++i) {
    composer_->InsertCharacterKeyEvent(
        GetKeyEvent(kKeys[i], GetStubProbableKeyEvent(kKeys[i][0], 0.9f)));
    EXPECT_FALSE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  }
  composer_->Backspace();
  for (size_t i = 0; i < std::size(kKeys); ++i) {
    composer_->InsertCharacterKeyEvent(
        GetKeyEvent(kKeys[i], ProbableKeyEvents()));
    EXPECT_TRUE(IsTypingCorrectorClearedOrInvalidated(*composer_));
  }
}

TEST_F(ComposerTest, GetRawString) {
  table_->AddRule("sa", "???", "");
  table_->AddRule("shi", "???", "");
  table_->AddRule("mi", "???", "");

  composer_->SetOutputMode(transliteration::HIRAGANA);

  composer_->InsertCharacter("sashimi");

  std::string output;
  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("?????????", output);

  std::string raw_string;
  composer_->GetRawString(&raw_string);
  EXPECT_EQ("sashimi", raw_string);

  std::string raw_sub_string;
  composer_->GetRawSubString(0, 2, &raw_sub_string);
  EXPECT_EQ("sashi", raw_sub_string);

  composer_->GetRawSubString(1, 1, &raw_sub_string);
  EXPECT_EQ("shi", raw_sub_string);
}

TEST_F(ComposerTest, SetPreeditTextForTestOnly) {
  std::string output;
  std::set<std::string> expanded;

  composer_->SetPreeditTextForTestOnly("???");

  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());

  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("???", output);

  composer_->GetStringForSubmission(&output);
  EXPECT_EQ("???", output);

  composer_->GetQueryForConversion(&output);
  EXPECT_EQ("???", output);

  composer_->GetQueryForPrediction(&output);
  EXPECT_EQ("???", output);

  composer_->GetQueriesForPrediction(&output, &expanded);
  EXPECT_EQ("???", output);
  EXPECT_TRUE(expanded.empty());

  composer_->Reset();

  composer_->SetPreeditTextForTestOnly("mo");

  EXPECT_EQ(transliteration::HALF_ASCII, composer_->GetInputMode());

  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("mo", output);

  composer_->GetStringForSubmission(&output);
  EXPECT_EQ("mo", output);

  composer_->GetQueryForConversion(&output);
  EXPECT_EQ("mo", output);

  composer_->GetQueryForPrediction(&output);
  EXPECT_EQ("mo", output);

  composer_->GetQueriesForPrediction(&output, &expanded);
  EXPECT_EQ("mo", output);

  EXPECT_TRUE(expanded.empty());

  composer_->Reset();

  composer_->SetPreeditTextForTestOnly("???");

  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());

  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("???", output);

  composer_->GetStringForSubmission(&output);
  EXPECT_EQ("???", output);

  composer_->GetQueryForConversion(&output);
  EXPECT_EQ("m", output);

  composer_->GetQueryForPrediction(&output);
  EXPECT_EQ("m", output);

  composer_->GetQueriesForPrediction(&output, &expanded);
  EXPECT_EQ("m", output);

  EXPECT_TRUE(expanded.empty());

  composer_->Reset();

  composer_->SetPreeditTextForTestOnly("??????");

  EXPECT_EQ(transliteration::HIRAGANA, composer_->GetInputMode());

  composer_->GetStringForPreedit(&output);
  EXPECT_EQ("??????", output);

  composer_->GetStringForSubmission(&output);
  EXPECT_EQ("??????", output);

  composer_->GetQueryForConversion(&output);
  EXPECT_EQ("???z", output);

  composer_->GetQueryForPrediction(&output);
  EXPECT_EQ("???z", output);

  composer_->GetQueriesForPrediction(&output, &expanded);
  EXPECT_EQ("???z", output);

  EXPECT_TRUE(expanded.empty());
}

TEST_F(ComposerTest, IsToggleable) {
  constexpr int kAttrs =
      TableAttribute::NEW_CHUNK | TableAttribute::NO_TRANSLITERATION;
  table_->AddRuleWithAttributes("1", "", "{?}???", kAttrs);
  table_->AddRule("{?}???1", "", "{?}???");
  table_->AddRule("{?}???{!}", "", "{*}???");

  EXPECT_FALSE(composer_->IsToggleable());

  ASSERT_TRUE(InsertKeyWithMode("1", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("???", GetPreedit(composer_.get()));
  EXPECT_TRUE(composer_->IsToggleable());

  ASSERT_TRUE(InsertKeyWithMode("1", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("???", GetPreedit(composer_.get()));
  EXPECT_TRUE(composer_->IsToggleable());

  composer_->InsertCommandCharacter(Composer::STOP_KEY_TOGGLING);
  EXPECT_EQ("???", GetPreedit(composer_.get()));
  EXPECT_FALSE(composer_->IsToggleable());

  composer_->Reset();
  ASSERT_TRUE(InsertKeyWithMode("1", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("???", GetPreedit(composer_.get()));
  composer_->SetNewInput();
  EXPECT_FALSE(composer_->IsToggleable());
}

TEST_F(ComposerTest, CheckTimeout) {
  table_->AddRule("1", "", "???");
  table_->AddRule("???{!}", "???", "");
  table_->AddRule("???1", "", "???");
  table_->AddRule("???{!}", "???", "");
  table_->AddRule("???1", "", "???");

  constexpr uint64_t kBaseSeconds = 86400;  // Epoch time + 1 day.
  mozc::ScopedClockMock clock(kBaseSeconds, 0);

  EXPECT_EQ(0, composer_->timeout_threshold_msec());

  ASSERT_TRUE(InsertKeyWithMode("1", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("???", GetPreedit(composer_.get()));

  clock->PutClockForward(3, 0);  // +3.0 sec

  // Because the threshold is not set, STOP_KEY_TOGGLING is not sent.
  ASSERT_TRUE(InsertKeyWithMode("1", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("???", GetPreedit(composer_.get()));

  // Set the threshold time.
  composer_->Reset();
  composer_->set_timeout_threshold_msec(1000);

  ASSERT_TRUE(InsertKeyWithMode("1", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("???", GetPreedit(composer_.get()));

  clock->PutClockForward(3, 0);  // +3.0 sec
  ASSERT_TRUE(InsertKeyWithMode("1", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("??????", GetPreedit(composer_.get()));

  clock->PutClockForward(0, 700'000);  // +0.7 sec
  ASSERT_TRUE(InsertKeyWithMode("1", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("??????", GetPreedit(composer_.get()));
}

TEST_F(ComposerTest, CheckTimeoutWithProtobuf) {
  table_->AddRule("1", "", "???");
  table_->AddRule("???{!}", "???", "");
  table_->AddRule("???1", "", "???");
  table_->AddRule("???{!}", "???", "");
  table_->AddRule("???1", "", "???");

  constexpr uint64_t kBaseSeconds = 86400;  // Epoch time + 1 day.
  mozc::ScopedClockMock clock(kBaseSeconds, 0);

  config_->set_composing_timeout_threshold_msec(500);
  composer_->Reset();  // The threshold should be updated to 500msec.

  uint64_t timestamp_msec = kBaseSeconds * 1000;

  KeyEvent key_event;
  key_event.set_key_code('1');
  key_event.set_timestamp_msec(timestamp_msec);
  composer_->InsertCharacterKeyEvent(key_event);
  EXPECT_EQ("???", GetPreedit(composer_.get()));

  clock->PutClockForward(0, 100'000);  // +0.1 sec in the global clock
  timestamp_msec += 3000;              // +3.0 sec in proto.
  key_event.set_timestamp_msec(timestamp_msec);
  composer_->InsertCharacterKeyEvent(key_event);
  EXPECT_EQ("??????", GetPreedit(composer_.get()));

  clock->PutClockForward(0, 100'000);  // +0.1 sec in the global clock
  timestamp_msec += 700;               // +0.7 sec in proto.
  key_event.set_timestamp_msec(timestamp_msec);
  composer_->InsertCharacterKeyEvent(key_event);
  EXPECT_EQ("?????????", GetPreedit(composer_.get()));

  clock->PutClockForward(3, 0);  // +3.0 sec in the global clock
  timestamp_msec += 100;         // +0.7 sec in proto.
  key_event.set_timestamp_msec(timestamp_msec);
  composer_->InsertCharacterKeyEvent(key_event);
  EXPECT_EQ("?????????", GetPreedit(composer_.get()));
}

TEST_F(ComposerTest, SimultaneousInput) {
  table_->AddRule("k", "", "???");      // k ??? ???
  table_->AddRule("???{!}", "???", "");  // k ??? ??? (timeout)
  table_->AddRule("d", "", "???");      // d ??? ???
  table_->AddRule("???{!}", "???", "");  // d ??? ??? (timeout)
  table_->AddRule("???k", "???", "");    // dk ??? ???
  table_->AddRule("???d", "???", "");    // kd ??? ???

  constexpr uint64_t kBaseSeconds = 86400;  // Epoch time + 1 day.
  mozc::ScopedClockMock clock(kBaseSeconds, 0);
  composer_->set_timeout_threshold_msec(50);

  ASSERT_TRUE(InsertKeyWithMode("k", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("???", GetPreedit(composer_.get()));

  clock->PutClockForward(0, 30'000);  // +30 msec (< 50)
  ASSERT_TRUE(InsertKeyWithMode("d", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("???", GetPreedit(composer_.get()));

  clock->PutClockForward(0, 30'000);  // +30 msec (< 50)
  ASSERT_TRUE(InsertKeyWithMode("k", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("??????", GetPreedit(composer_.get()));

  clock->PutClockForward(0, 200'000);  // +200 msec (> 50)
  ASSERT_TRUE(InsertKeyWithMode("d", commands::HIRAGANA, composer_.get()));
  EXPECT_EQ("?????????", GetPreedit(composer_.get()));
}
}  // namespace composer
}  // namespace mozc
