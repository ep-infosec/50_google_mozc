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

#include "prediction/user_history_predictor.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/clock_mock.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/password_manager.h"
#include "base/port.h"
#include "base/system_util.h"
#include "base/util.h"
#include "composer/composer.h"
#include "composer/table.h"
#include "config/config_handler.h"
#include "converter/segments.h"
#include "data_manager/testing/mock_data_manager.h"
#include "dictionary/dictionary_mock.h"
#include "dictionary/suppression_dictionary.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "session/request_test_util.h"
#include "storage/encrypted_string_storage.h"
#include "testing/base/public/gmock.h"
#include "testing/base/public/googletest.h"
#include "testing/base/public/gunit.h"
#include "usage_stats/usage_stats.h"
#include "usage_stats/usage_stats_testing_util.h"
#include "absl/flags/flag.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace mozc {
namespace {

using commands::Request;
using config::Config;
using dictionary::DictionaryMock;
using dictionary::SuppressionDictionary;
using dictionary::Token;

}  // namespace

class UserHistoryPredictorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SystemUtil::SetUserProfileDirectory(absl::GetFlag(FLAGS_test_tmpdir));
    request_ = std::make_unique<Request>();
    config_ = std::make_unique<Config>();
    config::ConfigHandler::GetDefaultConfig(config_.get());
    table_ = std::make_unique<composer::Table>();
    composer_ = std::make_unique<composer::Composer>(
        table_.get(), request_.get(), config_.get());
    convreq_ = std::make_unique<ConversionRequest>(
        composer_.get(), request_.get(), config_.get());
    convreq_->set_max_user_history_prediction_candidates_size(10);
    convreq_->set_max_user_history_prediction_candidates_size_for_zero_query(
        10);
    data_and_predictor_.reset(CreateDataAndPredictor());

    mozc::usage_stats::UsageStats::ClearAllStatsForTest();
  }

  void TearDown() override {
    mozc::usage_stats::UsageStats::ClearAllStatsForTest();
  }

  UserHistoryPredictor *GetUserHistoryPredictor() {
    return data_and_predictor_->predictor.get();
  }

  void WaitForSyncer(UserHistoryPredictor *predictor) {
    predictor->WaitForSyncer();
  }

  UserHistoryPredictor *GetUserHistoryPredictorWithClearedHistory() {
    UserHistoryPredictor *predictor = data_and_predictor_->predictor.get();
    predictor->WaitForSyncer();
    predictor->ClearAllHistory();
    predictor->WaitForSyncer();
    return predictor;
  }

  DictionaryMock *GetDictionaryMock() {
    return data_and_predictor_->dictionary.get();
  }

  SuppressionDictionary *GetSuppressionDictionary() {
    return data_and_predictor_->suppression_dictionary.get();
  }

  bool IsSuggested(UserHistoryPredictor *predictor, const std::string &key,
                   const std::string &value) {
    ConversionRequest conversion_request;
    Segments segments;
    MakeSegmentsForSuggestion(key, &segments);
    conversion_request.set_request_type(ConversionRequest::SUGGESTION);
    return predictor->PredictForRequest(conversion_request, &segments) &&
           FindCandidateByValue(value, segments);
  }

  bool IsPredicted(UserHistoryPredictor *predictor, const std::string &key,
                   const std::string &value) {
    ConversionRequest conversion_request;
    Segments segments;
    MakeSegmentsForPrediction(key, &segments);
    conversion_request.set_request_type(ConversionRequest::PREDICTION);
    return predictor->PredictForRequest(conversion_request, &segments) &&
           FindCandidateByValue(value, segments);
  }

  bool IsSuggestedAndPredicted(UserHistoryPredictor *predictor,
                               const std::string &key,
                               const std::string &value) {
    return IsSuggested(predictor, key, value) &&
           IsPredicted(predictor, key, value);
  }

  static UserHistoryPredictor::Entry *InsertEntry(
      UserHistoryPredictor *predictor, const std::string &key,
      const std::string &value) {
    UserHistoryPredictor::Entry *e =
        &predictor->dic_->Insert(predictor->Fingerprint(key, value))->value;
    e->set_key(key);
    e->set_value(value);
    e->set_removed(false);
    return e;
  }

  static UserHistoryPredictor::Entry *AppendEntry(
      UserHistoryPredictor *predictor, const std::string &key,
      const std::string &value, UserHistoryPredictor::Entry *prev) {
    prev->add_next_entries()->set_entry_fp(predictor->Fingerprint(key, value));
    UserHistoryPredictor::Entry *e = InsertEntry(predictor, key, value);
    return e;
  }

  static size_t EntrySize(const UserHistoryPredictor &predictor) {
    return predictor.dic_->Size();
  }

  static bool LoadStorage(UserHistoryPredictor *predictor,
                          const UserHistoryStorage &history) {
    return predictor->Load(history);
  }

  static bool IsConnected(const UserHistoryPredictor::Entry &prev,
                          const UserHistoryPredictor::Entry &next) {
    const uint32_t fp =
        UserHistoryPredictor::Fingerprint(next.key(), next.value());
    for (size_t i = 0; i < prev.next_entries_size(); ++i) {
      if (prev.next_entries(i).entry_fp() == fp) {
        return true;
      }
    }
    return false;
  }

  // Helper function to create a test case for bigram history deletion.
  void InitHistory_JapaneseInput(UserHistoryPredictor *predictor,
                                 UserHistoryPredictor::Entry **japaneseinput,
                                 UserHistoryPredictor::Entry **japanese,
                                 UserHistoryPredictor::Entry **input) {
    // Make the history for ("japaneseinput", "JapaneseInput"). It's assumed
    // that this sentence consists of two segments, "japanese" and "input". So,
    // the following history entries are constructed:
    //   ("japaneseinput", "JapaneseInput")  // Unigram
    //   ("japanese", "Japanese") --- ("input", "Input")  // Bigram chain
    *japaneseinput = InsertEntry(predictor, "japaneseinput", "JapaneseInput");
    *japanese = InsertEntry(predictor, "japanese", "Japanese");
    *input = AppendEntry(predictor, "input", "Input", *japanese);
    (*japaneseinput)->set_last_access_time(1);
    (*japanese)->set_last_access_time(1);
    (*input)->set_last_access_time(1);

    // Check the predictor functionality for the above history structure.
    EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
    EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
    EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "input", "Input"));
  }

  // Helper function to create a test case for trigram history deletion.
  void InitHistory_JapaneseInputMethod(
      UserHistoryPredictor *predictor,
      UserHistoryPredictor::Entry **japaneseinputmethod,
      UserHistoryPredictor::Entry **japanese,
      UserHistoryPredictor::Entry **input,
      UserHistoryPredictor::Entry **method) {
    // Make the history for ("japaneseinputmethod", "JapaneseInputMethod"). It's
    // assumed that this sentence consists of three segments, "japanese",
    // "input" and "method". So, the following history entries are constructed:
    //   ("japaneseinputmethod", "JapaneseInputMethod")  // Unigram
    //   ("japanese", "Japanese") -- ("input", "Input") -- ("method", "Method")
    *japaneseinputmethod =
        InsertEntry(predictor, "japaneseinputmethod", "JapaneseInputMethod");
    *japanese = InsertEntry(predictor, "japanese", "Japanese");
    *input = AppendEntry(predictor, "input", "Input", *japanese);
    *method = AppendEntry(predictor, "method", "Method", *input);
    (*japaneseinputmethod)->set_last_access_time(1);
    (*japanese)->set_last_access_time(1);
    (*input)->set_last_access_time(1);
    (*method)->set_last_access_time(1);

    // Check the predictor functionality for the above history structure.
    EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
    EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
    EXPECT_TRUE(
        IsSuggestedAndPredicted(predictor, "japan", "JapaneseInputMethod"));
    EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "Input"));
    EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "InputMethod"));
    EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "meth", "Method"));
  }

  void AddSegmentForSuggestion(const std::string &key, Segments *segments) {
    convreq_->set_request_type(ConversionRequest::SUGGESTION);
    Segment *seg = segments->add_segment();
    seg->set_key(key);
    seg->set_segment_type(Segment::FIXED_VALUE);
  }

  void MakeSegmentsForSuggestion(const std::string &key, Segments *segments) {
    segments->Clear();
    AddSegmentForSuggestion(key, segments);
  }

  void SetUpInputForSuggestion(const std::string &key,
                               composer::Composer *composer,
                               Segments *segments) {
    composer->Reset();
    composer->SetPreeditTextForTestOnly(key);
    MakeSegmentsForSuggestion(key, segments);
  }

  void PrependHistorySegments(const std::string &key, const std::string &value,
                              Segments *segments) {
    Segment *seg = segments->push_front_segment();
    seg->set_segment_type(Segment::HISTORY);
    seg->set_key(key);
    Segment::Candidate *c = seg->add_candidate();
    c->key = key;
    c->content_key = key;
    c->value = value;
    c->content_value = value;
  }

  void SetUpInputForSuggestionWithHistory(const std::string &key,
                                          const std::string &hist_key,
                                          const std::string &hist_value,
                                          composer::Composer *composer,
                                          Segments *segments) {
    SetUpInputForSuggestion(key, composer, segments);
    PrependHistorySegments(hist_key, hist_value, segments);
  }

  void AddSegmentForPrediction(const std::string &key, Segments *segments) {
    convreq_->set_request_type(ConversionRequest::PREDICTION);
    Segment *seg = segments->add_segment();
    seg->set_key(key);
    seg->set_segment_type(Segment::FIXED_VALUE);
  }

  void MakeSegmentsForPrediction(const std::string &key, Segments *segments) {
    segments->Clear();
    AddSegmentForPrediction(key, segments);
  }

  void SetUpInputForPrediction(const std::string &key,
                               composer::Composer *composer,
                               Segments *segments) {
    composer->Reset();
    composer->SetPreeditTextForTestOnly(key);
    MakeSegmentsForPrediction(key, segments);
  }

  void SetUpInputForPredictionWithHistory(const std::string &key,
                                          const std::string &hist_key,
                                          const std::string &hist_value,
                                          composer::Composer *composer,
                                          Segments *segments) {
    SetUpInputForPrediction(key, composer, segments);
    PrependHistorySegments(hist_key, hist_value, segments);
  }

  void AddSegmentForConversion(const std::string &key, Segments *segments) {
    convreq_->set_request_type(ConversionRequest::CONVERSION);
    Segment *seg = segments->add_segment();
    seg->set_key(key);
    seg->set_segment_type(Segment::FIXED_VALUE);
  }

  void MakeSegmentsForConversion(const std::string &key, Segments *segments) {
    segments->Clear();
    AddSegmentForConversion(key, segments);
  }

  void SetUpInputForConversion(const std::string &key,
                               composer::Composer *composer,
                               Segments *segments) {
    composer->Reset();
    composer->SetPreeditTextForTestOnly(key);
    MakeSegmentsForConversion(key, segments);
  }

  void SetUpInputForConversionWithHistory(const std::string &key,
                                          const std::string &hist_key,
                                          const std::string &hist_value,
                                          composer::Composer *composer,
                                          Segments *segments) {
    SetUpInputForConversion(key, composer, segments);
    PrependHistorySegments(hist_key, hist_value, segments);
  }

  void AddCandidate(size_t index, const std::string &value,
                    Segments *segments) {
    Segment::Candidate *candidate =
        segments->mutable_segment(index)->add_candidate();
    CHECK(candidate);
    candidate->Init();
    candidate->value = value;
    candidate->content_value = value;
    candidate->key = segments->segment(index).key();
    candidate->content_key = segments->segment(index).key();
  }

  void AddCandidateWithDescription(size_t index, const std::string &value,
                                   const std::string &desc,
                                   Segments *segments) {
    Segment::Candidate *candidate =
        segments->mutable_segment(index)->add_candidate();
    CHECK(candidate);
    candidate->Init();
    candidate->value = value;
    candidate->content_value = value;
    candidate->key = segments->segment(index).key();
    candidate->content_key = segments->segment(index).key();
    candidate->description = desc;
  }

  void AddCandidate(const std::string &value, Segments *segments) {
    AddCandidate(0, value, segments);
  }

  void AddCandidateWithDescription(const std::string &value,
                                   const std::string &desc,
                                   Segments *segments) {
    AddCandidateWithDescription(0, value, desc, segments);
  }

  bool FindCandidateByValue(const std::string &value,
                            const Segments &segments) {
    for (size_t i = 0; i < segments.conversion_segment(0).candidates_size();
         ++i) {
      if (segments.conversion_segment(0).candidate(i).value == value) {
        return true;
      }
    }
    return false;
  }

  std::unique_ptr<composer::Composer> composer_;
  std::unique_ptr<composer::Table> table_;
  std::unique_ptr<ConversionRequest> convreq_;
  std::unique_ptr<Config> config_;
  std::unique_ptr<Request> request_;

 private:
  struct DataAndPredictor {
    std::unique_ptr<DictionaryMock> dictionary;
    std::unique_ptr<SuppressionDictionary> suppression_dictionary;
    std::unique_ptr<UserHistoryPredictor> predictor;
    dictionary::PosMatcher pos_matcher;
  };

  DataAndPredictor *CreateDataAndPredictor() const {
    DataAndPredictor *ret = new DataAndPredictor;
    testing::MockDataManager data_manager;
    ret->dictionary = std::make_unique<DictionaryMock>();
    ret->suppression_dictionary = std::make_unique<SuppressionDictionary>();
    ret->pos_matcher.Set(data_manager.GetPosMatcherData());
    ret->predictor = std::make_unique<UserHistoryPredictor>(
        ret->dictionary.get(), &ret->pos_matcher,
        ret->suppression_dictionary.get(), false);
    ret->predictor->WaitForSyncer();
    return ret;
  }

  std::unique_ptr<DataAndPredictor> data_and_predictor_;
  mozc::usage_stats::scoped_usage_stats_enabler usage_stats_enabler_;
};

TEST_F(UserHistoryPredictorTest, UserHistoryPredictorTest) {
  {
    UserHistoryPredictor *predictor = GetUserHistoryPredictor();
    WaitForSyncer(predictor);

    // Nothing happen
    {
      Segments segments;
      SetUpInputForSuggestion("?????????", composer_.get(), &segments);
      EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
      EXPECT_EQ(0, segments.segment(0).candidates_size());
    }

    // Nothing happen
    {
      Segments segments;
      SetUpInputForSuggestion("?????????", composer_.get(), &segments);
      EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
      EXPECT_EQ(0, segments.segment(0).candidates_size());
    }

    // Insert two items
    {
      Segments segments;
      SetUpInputForSuggestion("???????????????????????????????????????", composer_.get(),
                              &segments);
      AddCandidate("???????????????????????????", &segments);
      predictor->Finish(*convreq_, &segments);

      segments.Clear();
      SetUpInputForSuggestion("????????????", composer_.get(), &segments);
      EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
      EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);
      EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
                  Segment::Candidate::USER_HISTORY_PREDICTOR);

      segments.Clear();
      SetUpInputForSuggestion("????????????", composer_.get(), &segments);
      EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
      EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);
      EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
                  Segment::Candidate::USER_HISTORY_PREDICTOR);
    }

    // Insert without learning (nothing happen).
    {
      config::Config::HistoryLearningLevel no_learning_levels[] = {
          config::Config::READ_ONLY, config::Config::NO_HISTORY};
      for (config::Config::HistoryLearningLevel level : no_learning_levels) {
        config_->set_history_learning_level(level);

        Segments segments;
        SetUpInputForSuggestion("??????????????????????????????", composer_.get(),
                                &segments);
        AddCandidate("????????????????????????", &segments);
        predictor->Finish(*convreq_, &segments);

        segments.Clear();
        SetUpInputForSuggestion("???????????????", composer_.get(), &segments);
        EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
        SetUpInputForSuggestion("???????????????", composer_.get(), &segments);
        EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
      }
      config_->set_history_learning_level(config::Config::DEFAULT_HISTORY);
    }

    // sync
    predictor->Sync();
    Util::Sleep(500);
  }

  // reload
  {
    UserHistoryPredictor *predictor = GetUserHistoryPredictor();
    WaitForSyncer(predictor);
    Segments segments;

    // turn off
    {
      Segments segments;
      config_->set_use_history_suggest(false);

      SetUpInputForSuggestion("????????????", composer_.get(), &segments);
      EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

      config_->set_use_history_suggest(true);
      config_->set_incognito_mode(true);

      SetUpInputForSuggestion("????????????", composer_.get(), &segments);
      EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

      config_->set_incognito_mode(false);
      config_->set_history_learning_level(config::Config::NO_HISTORY);

      SetUpInputForSuggestion("????????????", composer_.get(), &segments);
      EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
    }

    // turn on
    { config::ConfigHandler::GetDefaultConfig(config_.get()); }

    // reproducesd
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);

    segments.Clear();
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);

    // Exact Match
    segments.Clear();
    SetUpInputForSuggestion("???????????????????????????????????????", composer_.get(),
                            &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);

    segments.Clear();
    SetUpInputForSuggestion("???????????????????????????????????????", composer_.get(),
                            &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);

    segments.Clear();
    SetUpInputForSuggestion("??????????????????????????????", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

    segments.Clear();
    SetUpInputForSuggestion("??????????????????????????????", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

    // Read only mode should show suggestion.
    {
      config_->set_history_learning_level(config::Config::READ_ONLY);
      SetUpInputForSuggestion("????????????", composer_.get(), &segments);
      EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
      EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);

      segments.Clear();
      SetUpInputForSuggestion("????????????", composer_.get(), &segments);
      EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
      EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);
      config_->set_history_learning_level(config::Config::DEFAULT_HISTORY);
    }

    // clear
    predictor->ClearAllHistory();
    WaitForSyncer(predictor);
  }

  // nothing happen
  {
    UserHistoryPredictor *predictor = GetUserHistoryPredictor();
    WaitForSyncer(predictor);
    Segments segments;

    // reproducesd
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
  }

  // nothing happen
  {
    UserHistoryPredictor *predictor = GetUserHistoryPredictor();
    WaitForSyncer(predictor);
    Segments segments;

    // reproducesd
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
  }
}

// We did not support such Segments which has multiple segments and
// has type != CONVERSION.
// To support such Segments, this test case is created separately.
TEST_F(UserHistoryPredictorTest, UserHistoryPredictorTestSuggestion) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Register input histories via Finish method.
  {
    Segments segments;
    SetUpInputForSuggestion("?????????", composer_.get(), &segments);
    AddCandidate(0, "?????????", &segments);
    AddSegmentForSuggestion("???", &segments);
    AddCandidate(1, "???", &segments);
    predictor->Finish(*convreq_, &segments);

    // All added items must be suggestion entries.
    const UserHistoryPredictor::DicCache::Element *element;
    for (element = predictor->dic_->Head(); element->next;
         element = element->next) {
      const user_history_predictor::UserHistory::Entry &entry = element->value;
      EXPECT_TRUE(entry.has_suggestion_freq() && entry.suggestion_freq() == 1);
      EXPECT_TRUE(!entry.has_conversion_freq() && entry.conversion_freq() == 0);
    }
  }

  // Obtain input histories via Predict method.
  {
    Segments segments;
    SetUpInputForSuggestion("??????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    std::set<std::string> expected_candidates;
    expected_candidates.insert("?????????");
    // We can get this entry even if Segmtnts's type is not CONVERSION.
    expected_candidates.insert("????????????");
    for (size_t i = 0; i < segments.segment(0).candidates_size(); ++i) {
      SCOPED_TRACE(segments.segment(0).candidate(i).value);
      EXPECT_EQ(
          1, expected_candidates.erase(segments.segment(0).candidate(i).value));
    }
  }
}

TEST_F(UserHistoryPredictorTest, DescriptionTest) {
#ifdef DEBUG
  constexpr char kDescription[] = "????????? History";
#else   // DEBUG
  constexpr char kDescription[] = "?????????";
#endif  // DEBUG

  {
    UserHistoryPredictor *predictor = GetUserHistoryPredictor();
    WaitForSyncer(predictor);

    // Insert two items
    {
      Segments segments;
      SetUpInputForConversion("???????????????????????????????????????", composer_.get(),
                              &segments);
      AddCandidateWithDescription("???????????????????????????", kDescription,
                                  &segments);
      predictor->Finish(*convreq_, &segments);

      SetUpInputForSuggestion("????????????", composer_.get(), &segments);
      EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
      EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);
      EXPECT_EQ(kDescription, segments.segment(0).candidate(0).description);

      segments.Clear();
      SetUpInputForPrediction("????????????", composer_.get(), &segments);
      EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
      EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);
      EXPECT_EQ(kDescription, segments.segment(0).candidate(0).description);
    }

    // sync
    predictor->Sync();
  }

  // reload
  {
    UserHistoryPredictor *predictor = GetUserHistoryPredictor();
    WaitForSyncer(predictor);
    Segments segments;

    // turn off
    {
      Segments segments;
      config_->set_use_history_suggest(false);
      WaitForSyncer(predictor);

      SetUpInputForSuggestion("????????????", composer_.get(), &segments);
      EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

      config_->set_use_history_suggest(true);
      config_->set_incognito_mode(true);

      SetUpInputForSuggestion("????????????", composer_.get(), &segments);
      EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
    }

    // turn on
    {
      config::ConfigHandler::GetDefaultConfig(config_.get());
      WaitForSyncer(predictor);
    }

    // reproducesd
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);
    EXPECT_EQ(kDescription, segments.segment(0).candidate(0).description);

    segments.Clear();
    SetUpInputForPrediction("????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);
    EXPECT_EQ(kDescription, segments.segment(0).candidate(0).description);

    // Exact Match
    segments.Clear();
    SetUpInputForSuggestion("???????????????????????????????????????", composer_.get(),
                            &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);
    EXPECT_EQ(kDescription, segments.segment(0).candidate(0).description);

    segments.Clear();
    SetUpInputForSuggestion("???????????????????????????????????????", composer_.get(),
                            &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);
    EXPECT_EQ(kDescription, segments.segment(0).candidate(0).description);

    // clear
    predictor->ClearAllHistory();
    WaitForSyncer(predictor);
  }

  // nothing happen
  {
    UserHistoryPredictor *predictor = GetUserHistoryPredictor();
    WaitForSyncer(predictor);
    Segments segments;

    // reproducesd
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

    SetUpInputForPrediction("????????????", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
  }

  // nothing happen
  {
    UserHistoryPredictor *predictor = GetUserHistoryPredictor();
    WaitForSyncer(predictor);
    Segments segments;

    // reproducesd
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

    SetUpInputForPrediction("????????????", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
  }
}

TEST_F(UserHistoryPredictorTest, UserHistoryPredictorUnusedHistoryTest) {
  {
    UserHistoryPredictor *predictor = GetUserHistoryPredictor();
    WaitForSyncer(predictor);

    Segments segments;
    SetUpInputForSuggestion("???????????????????????????????????????", composer_.get(),
                            &segments);
    AddCandidate("???????????????????????????", &segments);

    // once
    predictor->Finish(*convreq_, &segments);

    segments.Clear();
    SetUpInputForConversion("????????????????????????", composer_.get(), &segments);
    AddCandidate("????????????", &segments);

    // conversion
    predictor->Finish(*convreq_, &segments);

    // sync
    predictor->Sync();
  }

  {
    UserHistoryPredictor *predictor = GetUserHistoryPredictor();
    WaitForSyncer(predictor);
    Segments segments;

    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);

    segments.Clear();
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("????????????", segments.segment(0).candidate(0).value);

    predictor->ClearUnusedHistory();
    WaitForSyncer(predictor);

    segments.Clear();
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);

    segments.Clear();
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

    predictor->Sync();
  }

  {
    UserHistoryPredictor *predictor = GetUserHistoryPredictor();
    WaitForSyncer(predictor);
    Segments segments;

    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);

    segments.Clear();
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
  }
}

TEST_F(UserHistoryPredictorTest, UserHistoryPredictorRevertTest) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments, segments2;
  SetUpInputForConversion("???????????????????????????????????????", composer_.get(),
                          &segments);
  AddCandidate("???????????????????????????", &segments);

  predictor->Finish(*convreq_, &segments);

  // Before Revert, Suggest works
  SetUpInputForSuggestion("????????????", composer_.get(), &segments2);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments2));
  EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);

  // Call revert here
  predictor->Revert(&segments);

  segments.Clear();
  SetUpInputForSuggestion("????????????", composer_.get(), &segments);

  EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ(0, segments.segment(0).candidates_size());

  EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ(0, segments.segment(0).candidates_size());
}

TEST_F(UserHistoryPredictorTest, UserHistoryPredictorClearTest) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictor();
  WaitForSyncer(predictor);

  // input "testtest" 10 times
  for (int i = 0; i < 10; ++i) {
    Segments segments;
    SetUpInputForConversion("testtest", composer_.get(), &segments);
    AddCandidate("??????????????????", &segments);
    predictor->Finish(*convreq_, &segments);
  }

  predictor->ClearAllHistory();
  WaitForSyncer(predictor);

  // input "testtest" 1 time
  for (int i = 0; i < 1; ++i) {
    Segments segments;
    SetUpInputForConversion("testtest", composer_.get(), &segments);
    AddCandidate("??????????????????", &segments);
    predictor->Finish(*convreq_, &segments);
  }

  // frequency is cleared as well.
  {
    Segments segments;
    SetUpInputForSuggestion("t", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

    segments.Clear();
    SetUpInputForSuggestion("testte", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  }
}

TEST_F(UserHistoryPredictorTest, UserHistoryPredictorTrailingPunctuation) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;

  SetUpInputForConversion("???????????????????????????????????????", composer_.get(),
                          &segments);

  AddCandidate(0, "???????????????????????????", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(1, "???", &segments);

  predictor->Finish(*convreq_, &segments);

  segments.Clear();
  SetUpInputForPrediction("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ(2, segments.segment(0).candidates_size());
  EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);
  EXPECT_EQ("??????????????????????????????", segments.segment(0).candidate(1).value);

  segments.Clear();
  SetUpInputForSuggestion("????????????", composer_.get(), &segments);

  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ(2, segments.segment(0).candidates_size());
  EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);
  EXPECT_EQ("??????????????????????????????", segments.segment(0).candidate(1).value);
}

TEST_F(UserHistoryPredictorTest, TrailingPunctuationMobile) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();
  commands::RequestForUnitTest::FillMobileRequest(request_.get());
  Segments segments;

  SetUpInputForConversion("?????????", composer_.get(), &segments);

  AddCandidate(0, "?????????", &segments);

  predictor->Finish(*convreq_, &segments);

  segments.Clear();

  SetUpInputForPrediction("??????", composer_.get(), &segments);
  EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
}

TEST_F(UserHistoryPredictorTest, HistoryToPunctuation) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;

  // Scenario 1: A user have committed "???" by prediction and then commit "???".
  // Then, the unigram "???" is learned but the bigram "??????" shouldn't.
  SetUpInputForPrediction("???", composer_.get(), &segments);
  AddCandidate(0, "???", &segments);
  predictor->Finish(*convreq_, &segments);
  segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

  AddSegmentForPrediction("???", &segments);
  AddCandidate(1, "???", &segments);
  predictor->Finish(*convreq_, &segments);

  segments.Clear();
  SetUpInputForPrediction("???", composer_.get(), &segments);  // "???"
  ASSERT_TRUE(predictor->PredictForRequest(*convreq_, &segments))
      << segments.DebugString();
  EXPECT_EQ("???", segments.segment(0).candidate(0).value);

  segments.Clear();

  // Scenario 2: the opposite case to Scenario 1, i.e., "??????".  Nothing is
  // suggested from symbol "???".
  SetUpInputForPrediction("???", composer_.get(), &segments);
  AddCandidate(0, "???", &segments);
  predictor->Finish(*convreq_, &segments);
  segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

  AddSegmentForPrediction("???", &segments);
  AddCandidate(1, "???", &segments);
  predictor->Finish(*convreq_, &segments);

  segments.Clear();
  SetUpInputForPrediction("???", composer_.get(), &segments);  // "???"
  EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments))
      << segments.DebugString();

  segments.Clear();

  // Scenario 3: If the history segment looks like a sentence and committed
  // value is a punctuation, the concatenated entry is also learned.
  SetUpInputForPrediction("????????????????????????", composer_.get(), &segments);
  AddCandidate(0, "??????????????????", &segments);
  predictor->Finish(*convreq_, &segments);
  segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

  AddSegmentForPrediction("???", &segments);
  AddCandidate(1, "???", &segments);
  predictor->Finish(*convreq_, &segments);

  segments.Clear();
  SetUpInputForPrediction("????????????", composer_.get(), &segments);
  ASSERT_TRUE(predictor->PredictForRequest(*convreq_, &segments))
      << segments.DebugString();
  EXPECT_EQ("??????????????????", segments.segment(0).candidate(0).value);
  EXPECT_EQ("?????????????????????", segments.segment(0).candidate(1).value);
}

TEST_F(UserHistoryPredictorTest, UserHistoryPredictorPrecedingPunctuation) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;

  SetUpInputForConversion("???", composer_.get(), &segments);
  AddCandidate(0, "???", &segments);

  AddSegmentForConversion("???????????????????????????????????????", &segments);

  AddCandidate(1, "???????????????????????????", &segments);

  predictor->Finish(*convreq_, &segments);

  segments.Clear();
  SetUpInputForPrediction("????????????", composer_.get(), &segments);

  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ(1, segments.segment(0).candidates_size());
  EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);

  segments.Clear();
  SetUpInputForSuggestion("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ(1, segments.segment(0).candidates_size());
  EXPECT_EQ("???????????????????????????", segments.segment(0).candidate(0).value);
}

namespace {
struct StartsWithPunctuationsTestData {
  const char *first_character;
  bool expected_result;
};
}  // namespace

TEST_F(UserHistoryPredictorTest, StartsWithPunctuations) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictor();
  const StartsWithPunctuationsTestData kTestCases[] = {
      {"???", false}, {"???", false},    {"???", false},
      {"???", false}, {"?????????", true},
  };

  for (size_t i = 0; i < std::size(kTestCases); ++i) {
    WaitForSyncer(predictor);
    predictor->ClearAllHistory();
    WaitForSyncer(predictor);

    Segments segments;
    const std::string first_char = kTestCases[i].first_character;
    {
      // Learn from two segments
      SetUpInputForConversion(first_char, composer_.get(), &segments);
      AddCandidate(0, first_char, &segments);
      AddSegmentForConversion("????????????????????????", &segments);
      AddCandidate(1, "???????????????", &segments);
      predictor->Finish(*convreq_, &segments);
    }
    segments.Clear();
    {
      // Learn from one segment
      SetUpInputForConversion(first_char + "????????????????????????", composer_.get(),
                              &segments);
      AddCandidate(0, first_char + "???????????????", &segments);
      predictor->Finish(*convreq_, &segments);
    }
    segments.Clear();
    {
      // Suggestion
      SetUpInputForSuggestion(first_char, composer_.get(), &segments);
      AddCandidate(0, first_char, &segments);
      EXPECT_EQ(kTestCases[i].expected_result,
                predictor->PredictForRequest(*convreq_, &segments))
          << "Suggest from " << first_char;
    }
    segments.Clear();
    {
      // Prediciton
      SetUpInputForPrediction(first_char, composer_.get(), &segments);
      EXPECT_EQ(kTestCases[i].expected_result,
                predictor->PredictForRequest(*convreq_, &segments))
          << "Predict from " << first_char;
    }
  }
}

TEST_F(UserHistoryPredictorTest, ZeroQuerySuggestionTest) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  request_->set_zero_query_suggestion(true);

  commands::Request non_zero_query_request;
  non_zero_query_request.set_zero_query_suggestion(false);
  ConversionRequest non_zero_query_conversion_request(
      composer_.get(), &non_zero_query_request, config_.get());

  Segments segments;

  // No history segments
  segments.Clear();
  SetUpInputForSuggestion("", composer_.get(), &segments);
  EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

  {
    segments.Clear();

    SetUpInputForConversion("????????????", composer_.get(), &segments);
    AddCandidate(0, "?????????", &segments);
    predictor->Finish(*convreq_, &segments);

    SetUpInputForConversionWithHistory("????????????", "????????????", "?????????",
                                       composer_.get(), &segments);
    AddCandidate(1, "?????????", &segments);
    predictor->Finish(*convreq_, &segments);

    SetUpInputForConversionWithHistory("????????????", "????????????", "?????????",
                                       composer_.get(), &segments);
    AddCandidate(1, "??????", &segments);
    Util::Sleep(2000);
    predictor->Finish(*convreq_, &segments);

    SetUpInputForConversionWithHistory("????????????", "????????????", "?????????",
                                       composer_.get(), &segments);
    AddCandidate(1, "??????", &segments);
    Util::Sleep(2000);
    predictor->Finish(*convreq_, &segments);

    // Zero query suggestion is disabled.
    SetUpInputForSuggestionWithHistory("", "????????????", "?????????",
                                       composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(non_zero_query_conversion_request,
                                              &segments));

    SetUpInputForSuggestionWithHistory("", "????????????", "?????????",
                                       composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    ASSERT_EQ(2, segments.segments_size());
    // last-pushed segment is "??????"
    EXPECT_EQ("??????", segments.segment(1).candidate(0).value);
    EXPECT_EQ("????????????", segments.segment(1).candidate(0).key);
    EXPECT_TRUE(segments.segment(1).candidate(0).source_info &
                Segment::Candidate::USER_HISTORY_PREDICTOR);

    for (const char *key : {"???", "???", "???", "???"}) {
      SetUpInputForSuggestionWithHistory(key, "????????????", "?????????",
                                         composer_.get(), &segments);
      EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    }
  }

  predictor->ClearAllHistory();
  WaitForSyncer(predictor);

  {
    segments.Clear();
    SetUpInputForConversion("????????????", composer_.get(), &segments);
    AddCandidate(0, "?????????", &segments);

    AddSegmentForConversion("????????????", &segments);
    AddCandidate(1, "?????????", &segments);
    predictor->Finish(*convreq_, &segments);

    segments.Clear();
    SetUpInputForConversion("????????????", composer_.get(), &segments);
    AddCandidate(0, "?????????", &segments);
    segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

    // Zero query suggestion is disabled.
    AddSegmentForSuggestion("", &segments);  // empty request
    EXPECT_FALSE(predictor->PredictForRequest(non_zero_query_conversion_request,
                                              &segments));

    segments.pop_back_segment();
    AddSegmentForSuggestion("", &segments);  // empty request
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

    segments.pop_back_segment();
    AddSegmentForSuggestion("???", &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

    segments.pop_back_segment();
    AddSegmentForSuggestion("???", &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  }
}

TEST_F(UserHistoryPredictorTest, MultiSegmentsMultiInput) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;

  SetUpInputForConversion("????????????", composer_.get(), &segments);
  AddCandidate(0, "?????????", &segments);
  predictor->Finish(*convreq_, &segments);
  segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

  AddSegmentForConversion("????????????", &segments);
  AddCandidate(1, "?????????", &segments);
  predictor->Finish(*convreq_, &segments);
  segments.mutable_segment(1)->set_segment_type(Segment::HISTORY);

  segments.clear_conversion_segments();
  AddSegmentForConversion("???????????????", &segments);
  AddCandidate(2, "?????????", &segments);
  predictor->Finish(*convreq_, &segments);
  segments.mutable_segment(2)->set_segment_type(Segment::HISTORY);

  segments.clear_conversion_segments();
  AddSegmentForConversion("?????????", &segments);
  AddCandidate(3, "??????", &segments);
  predictor->Finish(*convreq_, &segments);
  segments.mutable_segment(3)->set_segment_type(Segment::HISTORY);

  segments.clear_conversion_segments();
  AddSegmentForConversion("????????????", &segments);
  AddCandidate(4, "????????????", &segments);
  predictor->Finish(*convreq_, &segments);

  segments.Clear();
  SetUpInputForSuggestion("???", composer_.get(), &segments);
  EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("?????????", composer_.get(), &segments);
  EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("??????????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("???????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("??????????????????????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("???????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  Util::Sleep(1000);

  // Add new entry "????????????????????????/??????????????????"
  segments.Clear();
  SetUpInputForConversion("????????????", composer_.get(), &segments);
  AddCandidate(0, "?????????", &segments);
  predictor->Finish(*convreq_, &segments);
  segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

  AddSegmentForConversion("????????????", &segments);
  AddCandidate(1, "?????????", &segments);
  predictor->Finish(*convreq_, &segments);
  segments.mutable_segment(1)->set_segment_type(Segment::HISTORY);

  segments.Clear();
  SetUpInputForSuggestion("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ("??????????????????", segments.segment(0).candidate(0).value);
  EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);
}

TEST_F(UserHistoryPredictorTest, MultiSegmentsSingleInput) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;

  SetUpInputForConversion("????????????", composer_.get(), &segments);
  AddCandidate(0, "?????????", &segments);

  AddSegmentForConversion("????????????", &segments);
  AddCandidate(1, "?????????", &segments);

  AddSegmentForConversion("???????????????", &segments);
  AddCandidate(2, "?????????", &segments);

  AddSegmentForConversion("?????????", &segments);
  AddCandidate(3, "??????", &segments);

  AddSegmentForConversion("????????????", &segments);
  AddCandidate(4, "????????????", &segments);

  predictor->Finish(*convreq_, &segments);

  segments.Clear();
  SetUpInputForSuggestion("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("???", composer_.get(), &segments);
  EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("??????????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("???????????????", composer_.get(), &segments);
  EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("???????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("??????????????????????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  segments.Clear();
  SetUpInputForSuggestion("???????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  Util::Sleep(1000);

  // Add new entry "????????????????????????/??????????????????"
  segments.Clear();
  SetUpInputForConversion("????????????", composer_.get(), &segments);
  AddCandidate(0, "?????????", &segments);
  predictor->Finish(*convreq_, &segments);
  segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

  AddSegmentForConversion("????????????", &segments);
  AddCandidate(1, "?????????", &segments);
  predictor->Finish(*convreq_, &segments);
  segments.mutable_segment(1)->set_segment_type(Segment::HISTORY);

  segments.Clear();
  SetUpInputForSuggestion("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ("??????????????????", segments.segment(0).candidate(0).value);
  EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);
}

TEST_F(UserHistoryPredictorTest, Regression2843371Case1) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;

  SetUpInputForConversion("??????????????????", composer_.get(), &segments);
  AddCandidate(0, "?????????", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(1, "???", &segments);

  AddSegmentForConversion("???????????????", &segments);
  AddCandidate(2, "????????????", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(3, "???", &segments);

  predictor->Finish(*convreq_, &segments);

  segments.Clear();

  Util::Sleep(1000);

  SetUpInputForConversion("???????????????", composer_.get(), &segments);
  AddCandidate(0, "???????????????", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(1, "???", &segments);

  AddSegmentForConversion("??????????????????", &segments);
  AddCandidate(2, "????????????", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(3, "???", &segments);

  predictor->Finish(*convreq_, &segments);

  segments.Clear();

  SetUpInputForSuggestion("?????????????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  EXPECT_EQ("????????????????????????", segments.segment(0).candidate(0).value);
  EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);
}

TEST_F(UserHistoryPredictorTest, Regression2843371Case2) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;

  SetUpInputForConversion("??????", composer_.get(), &segments);
  AddCandidate(0, "??????", &segments);

  AddSegmentForConversion("(", &segments);
  AddCandidate(1, "(", &segments);

  AddSegmentForConversion("???????????????", &segments);
  AddCandidate(2, "??????", &segments);

  AddSegmentForConversion(")", &segments);
  AddCandidate(3, ")", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(4, "???", &segments);

  AddSegmentForConversion("??????", &segments);
  AddCandidate(5, "??????", &segments);

  AddSegmentForConversion("(", &segments);
  AddCandidate(6, "(", &segments);

  AddSegmentForConversion("??????????????????", &segments);
  AddCandidate(7, "?????????", &segments);

  AddSegmentForConversion(")", &segments);
  AddCandidate(8, ")", &segments);

  AddSegmentForConversion("????????????", &segments);
  AddCandidate(9, "????????????", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(10, "???", &segments);

  predictor->Finish(*convreq_, &segments);

  segments.Clear();

  SetUpInputForSuggestion("??????(", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ("??????(??????", segments.segment(0).candidate(0).value);
  EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);

  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  EXPECT_EQ("??????(??????", segments.segment(0).candidate(0).value);
  EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);
}

TEST_F(UserHistoryPredictorTest, Regression2843371Case3) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;

  SetUpInputForConversion("???", composer_.get(), &segments);
  AddCandidate(0, "???", &segments);

  AddSegmentForConversion("??????", &segments);
  AddCandidate(1, "???", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(2, "???", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(3, "???", &segments);

  AddSegmentForConversion("?????????", &segments);
  AddCandidate(4, "??????", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(5, "???", &segments);

  predictor->Finish(*convreq_, &segments);

  Util::Sleep(2000);

  segments.Clear();

  SetUpInputForConversion("???", composer_.get(), &segments);
  AddCandidate(0, "???", &segments);

  AddSegmentForConversion("??????", &segments);
  AddCandidate(1, "???", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(2, "???", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(3, "???", &segments);

  AddSegmentForConversion("?????????", &segments);
  AddCandidate(4, "??????", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(5, "???", &segments);

  predictor->Finish(*convreq_, &segments);

  segments.Clear();

  SetUpInputForSuggestion("???????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  EXPECT_EQ("??????????????????", segments.segment(0).candidate(0).value);
  EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);
}

TEST_F(UserHistoryPredictorTest, Regression2843775) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;

  SetUpInputForConversion("????????????", composer_.get(), &segments);
  AddCandidate(0, "????????????", &segments);

  AddSegmentForConversion("????????????????????????????????????", &segments);
  AddCandidate(1, "?????????????????????????????????", &segments);

  predictor->Finish(*convreq_, &segments);

  segments.Clear();

  SetUpInputForSuggestion("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  EXPECT_EQ("?????????????????????????????????????????????",
            segments.segment(0).candidate(0).value);
  EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);
}

TEST_F(UserHistoryPredictorTest, DuplicateString) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;

  SetUpInputForConversion("????????????", composer_.get(), &segments);
  AddCandidate(0, "????????????", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(1, "???", &segments);

  AddSegmentForConversion("???????????????", &segments);
  AddCandidate(2, "??????", &segments);

  AddSegmentForConversion("??????", &segments);
  AddCandidate(3, "??????", &segments);

  AddSegmentForConversion("???????????????", &segments);
  AddCandidate(4, "???????????????", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(5, "???", &segments);

  AddSegmentForConversion("???????????????", &segments);
  AddCandidate(6, "?????????", &segments);

  AddSegmentForConversion("???", &segments);
  AddCandidate(7, "???", &segments);

  predictor->Finish(*convreq_, &segments);

  segments.Clear();

  SetUpInputForSuggestion("???????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  for (int i = 0; i < segments.segment(0).candidates_size(); ++i) {
    EXPECT_EQ(std::string::npos,
              segments.segment(0).candidate(i).value.find(
                  "??????"));  // "??????" should not be found
  }

  segments.Clear();

  SetUpInputForSuggestion("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));

  for (int i = 0; i < segments.segment(0).candidates_size(); ++i) {
    EXPECT_EQ(std::string::npos,
              segments.segment(0).candidate(i).value.find("????????????????????????"));
  }
}

struct Command {
  enum Type {
    LOOKUP,
    INSERT,
    SYNC,
    WAIT,
  };
  Type type;
  std::string key;
  std::string value;
  Command() : type(LOOKUP) {}
};

TEST_F(UserHistoryPredictorTest, SyncTest) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictor();
  WaitForSyncer(predictor);

  std::vector<Command> commands(10000);
  for (size_t i = 0; i < commands.size(); ++i) {
    commands[i].key = std::to_string(static_cast<uint32_t>(i)) + "key";
    commands[i].value = std::to_string(static_cast<uint32_t>(i)) + "value";
    const int n = Util::Random(100);
    if (n == 0) {
      commands[i].type = Command::WAIT;
    } else if (n < 10) {
      commands[i].type = Command::SYNC;
    } else if (n < 50) {
      commands[i].type = Command::INSERT;
    } else {
      commands[i].type = Command::LOOKUP;
    }
  }

  // Kind of stress test
  Segments segments;
  for (size_t i = 0; i < commands.size(); ++i) {
    switch (commands[i].type) {
      case Command::SYNC:
        predictor->Sync();
        break;
      case Command::WAIT:
        WaitForSyncer(predictor);
        break;
      case Command::INSERT:
        segments.Clear();
        SetUpInputForConversion(commands[i].key, composer_.get(), &segments);
        AddCandidate(commands[i].value, &segments);
        predictor->Finish(*convreq_, &segments);
        break;
      case Command::LOOKUP:
        segments.Clear();
        SetUpInputForSuggestion(commands[i].key, composer_.get(), &segments);
        predictor->PredictForRequest(*convreq_, &segments);
        break;
      default:
        break;
    }
  }
}

TEST_F(UserHistoryPredictorTest, GetMatchTypeTest) {
  EXPECT_EQ(UserHistoryPredictor::NO_MATCH,
            UserHistoryPredictor::GetMatchType("test", ""));

  EXPECT_EQ(UserHistoryPredictor::NO_MATCH,
            UserHistoryPredictor::GetMatchType("", ""));

  EXPECT_EQ(UserHistoryPredictor::LEFT_EMPTY_MATCH,
            UserHistoryPredictor::GetMatchType("", "test"));

  EXPECT_EQ(UserHistoryPredictor::NO_MATCH,
            UserHistoryPredictor::GetMatchType("foo", "bar"));

  EXPECT_EQ(UserHistoryPredictor::EXACT_MATCH,
            UserHistoryPredictor::GetMatchType("foo", "foo"));

  EXPECT_EQ(UserHistoryPredictor::LEFT_PREFIX_MATCH,
            UserHistoryPredictor::GetMatchType("foo", "foobar"));

  EXPECT_EQ(UserHistoryPredictor::RIGHT_PREFIX_MATCH,
            UserHistoryPredictor::GetMatchType("foobar", "foo"));
}

TEST_F(UserHistoryPredictorTest, FingerPrintTest) {
  constexpr char kKey[] = "abc";
  constexpr char kValue[] = "ABC";

  UserHistoryPredictor::Entry entry;
  entry.set_key(kKey);
  entry.set_value(kValue);

  const uint32_t entry_fp1 = UserHistoryPredictor::Fingerprint(kKey, kValue);
  const uint32_t entry_fp2 = UserHistoryPredictor::EntryFingerprint(entry);

  const uint32_t entry_fp3 = UserHistoryPredictor::Fingerprint(
      kKey, kValue, UserHistoryPredictor::Entry::DEFAULT_ENTRY);

  const uint32_t entry_fp4 = UserHistoryPredictor::Fingerprint(
      kKey, kValue, UserHistoryPredictor::Entry::CLEAN_ALL_EVENT);

  const uint32_t entry_fp5 = UserHistoryPredictor::Fingerprint(
      kKey, kValue, UserHistoryPredictor::Entry::CLEAN_UNUSED_EVENT);

  Segment segment;
  segment.set_key(kKey);
  Segment::Candidate *c = segment.add_candidate();
  c->key = kKey;
  c->content_key = kKey;
  c->value = kValue;
  c->content_value = kValue;

  const uint32_t segment_fp = UserHistoryPredictor::SegmentFingerprint(segment);

  Segment segment2;
  segment2.set_key("ab");
  Segment::Candidate *c2 = segment2.add_candidate();
  c2->key = kKey;
  c2->content_key = kKey;
  c2->value = kValue;
  c2->content_value = kValue;

  const uint32_t segment_fp2 =
      UserHistoryPredictor::SegmentFingerprint(segment2);

  EXPECT_EQ(entry_fp1, entry_fp2);
  EXPECT_EQ(entry_fp1, entry_fp3);
  EXPECT_NE(entry_fp1, entry_fp4);
  EXPECT_NE(entry_fp1, entry_fp5);
  EXPECT_NE(entry_fp4, entry_fp5);
  EXPECT_EQ(segment_fp, entry_fp2);
  EXPECT_EQ(segment_fp, entry_fp1);
  EXPECT_EQ(segment_fp, segment_fp2);
}

TEST_F(UserHistoryPredictorTest, Uint32ToStringTest) {
  EXPECT_EQ(123, UserHistoryPredictor::StringToUint32(
                     UserHistoryPredictor::Uint32ToString(123)));

  EXPECT_EQ(12141, UserHistoryPredictor::StringToUint32(
                       UserHistoryPredictor::Uint32ToString(12141)));

  for (uint32_t i = 0; i < 10000; ++i) {
    EXPECT_EQ(i, UserHistoryPredictor::StringToUint32(
                     UserHistoryPredictor::Uint32ToString(i)));
  }

  // invalid input
  EXPECT_EQ(0, UserHistoryPredictor::StringToUint32(""));

  // not 4byte
  EXPECT_EQ(0, UserHistoryPredictor::StringToUint32("abcdef"));
}

TEST_F(UserHistoryPredictorTest, GetScore) {
  // latest value has higher score.
  {
    UserHistoryPredictor::Entry entry1, entry2;

    entry1.set_key("abc");
    entry1.set_value("ABC");
    entry1.set_last_access_time(10);

    entry2.set_key("foo");
    entry2.set_value("ABC");
    entry2.set_last_access_time(20);

    EXPECT_GT(UserHistoryPredictor::GetScore(entry2),
              UserHistoryPredictor::GetScore(entry1));
  }

  // shorter value has higher score.
  {
    UserHistoryPredictor::Entry entry1, entry2;

    entry1.set_key("abc");
    entry1.set_value("ABC");
    entry1.set_last_access_time(10);

    entry2.set_key("foo");
    entry2.set_value("ABCD");
    entry2.set_last_access_time(10);

    EXPECT_GT(UserHistoryPredictor::GetScore(entry1),
              UserHistoryPredictor::GetScore(entry2));
  }

  // bigram boost makes the entry stronger
  {
    UserHistoryPredictor::Entry entry1, entry2;

    entry1.set_key("abc");
    entry1.set_value("ABC");
    entry1.set_last_access_time(10);

    entry2.set_key("foo");
    entry2.set_value("ABC");
    entry2.set_last_access_time(10);
    entry2.set_bigram_boost(true);

    EXPECT_GT(UserHistoryPredictor::GetScore(entry2),
              UserHistoryPredictor::GetScore(entry1));
  }

  // bigram boost makes the entry stronger
  {
    UserHistoryPredictor::Entry entry1, entry2;

    entry1.set_key("abc");
    entry1.set_value("ABCD");
    entry1.set_last_access_time(10);
    entry1.set_bigram_boost(true);

    entry2.set_key("foo");
    entry2.set_value("ABC");
    entry2.set_last_access_time(50);

    EXPECT_GT(UserHistoryPredictor::GetScore(entry1),
              UserHistoryPredictor::GetScore(entry2));
  }
}

TEST_F(UserHistoryPredictorTest, IsValidEntry) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictor();

  UserHistoryPredictor::Entry entry;

  EXPECT_TRUE(predictor->IsValidEntry(entry));

  entry.set_key("key");
  entry.set_value("value");

  EXPECT_TRUE(predictor->IsValidEntry(entry));
  EXPECT_TRUE(predictor->IsValidEntryIgnoringRemovedField(entry));

  entry.set_removed(true);
  EXPECT_FALSE(predictor->IsValidEntry(entry));
  EXPECT_TRUE(predictor->IsValidEntryIgnoringRemovedField(entry));

  entry.set_removed(false);
  EXPECT_TRUE(predictor->IsValidEntry(entry));
  EXPECT_TRUE(predictor->IsValidEntryIgnoringRemovedField(entry));

  entry.set_entry_type(UserHistoryPredictor::Entry::CLEAN_ALL_EVENT);
  EXPECT_FALSE(predictor->IsValidEntry(entry));
  EXPECT_FALSE(predictor->IsValidEntryIgnoringRemovedField(entry));

  entry.set_entry_type(UserHistoryPredictor::Entry::CLEAN_UNUSED_EVENT);
  EXPECT_FALSE(predictor->IsValidEntry(entry));
  EXPECT_FALSE(predictor->IsValidEntryIgnoringRemovedField(entry));

  entry.set_removed(true);
  EXPECT_FALSE(predictor->IsValidEntry(entry));
  EXPECT_FALSE(predictor->IsValidEntryIgnoringRemovedField(entry));

  entry.Clear();
  EXPECT_TRUE(predictor->IsValidEntry(entry));
  EXPECT_TRUE(predictor->IsValidEntryIgnoringRemovedField(entry));

  entry.Clear();
  entry.set_key("key");
  entry.set_value("value");
  entry.set_description("?????????");
  EXPECT_TRUE(predictor->IsValidEntry(entry));
  EXPECT_TRUE(predictor->IsValidEntryIgnoringRemovedField(entry));

  // An android pua emoji. It is obsolete and should return false.
  Util::Ucs4ToUtf8(0xFE000, entry.mutable_value());
  EXPECT_FALSE(predictor->IsValidEntry(entry));
  EXPECT_FALSE(predictor->IsValidEntryIgnoringRemovedField(entry));

  SuppressionDictionary *d = GetSuppressionDictionary();
  DCHECK(d);
  d->Lock();
  d->AddEntry("foo", "bar");
  d->UnLock();

  entry.set_key("key");
  entry.set_value("value");
  EXPECT_TRUE(predictor->IsValidEntry(entry));
  EXPECT_TRUE(predictor->IsValidEntryIgnoringRemovedField(entry));

  entry.set_key("foo");
  entry.set_value("bar");
  EXPECT_FALSE(predictor->IsValidEntry(entry));
  EXPECT_FALSE(predictor->IsValidEntryIgnoringRemovedField(entry));

  d->Lock();
  d->Clear();
  d->UnLock();
}

TEST_F(UserHistoryPredictorTest, IsValidSuggestion) {
  UserHistoryPredictor::Entry entry;

  EXPECT_FALSE(UserHistoryPredictor::IsValidSuggestion(
      UserHistoryPredictor::DEFAULT, 1, entry));

  entry.set_bigram_boost(true);
  EXPECT_TRUE(UserHistoryPredictor::IsValidSuggestion(
      UserHistoryPredictor::DEFAULT, 1, entry));

  entry.set_bigram_boost(false);
  EXPECT_TRUE(UserHistoryPredictor::IsValidSuggestion(
      UserHistoryPredictor::ZERO_QUERY_SUGGESTION, 1, entry));

  entry.set_bigram_boost(false);
  entry.set_conversion_freq(10);
  EXPECT_TRUE(UserHistoryPredictor::IsValidSuggestion(
      UserHistoryPredictor::DEFAULT, 1, entry));
}

TEST_F(UserHistoryPredictorTest, EntryPriorityQueueTest) {
  // removed automatically
  constexpr int kSize = 10000;
  {
    UserHistoryPredictor::EntryPriorityQueue queue;
    for (int i = 0; i < 10000; ++i) {
      EXPECT_NE(nullptr, queue.NewEntry());
    }
  }

  {
    UserHistoryPredictor::EntryPriorityQueue queue;
    std::vector<UserHistoryPredictor::Entry *> expected;
    for (int i = 0; i < kSize; ++i) {
      UserHistoryPredictor::Entry *entry = queue.NewEntry();
      entry->set_key("test" + std::to_string(i));
      entry->set_value("test" + std::to_string(i));
      entry->set_last_access_time(i + 1000);
      expected.push_back(entry);
      EXPECT_TRUE(queue.Push(entry));
    }

    int n = kSize - 1;
    while (true) {
      const UserHistoryPredictor::Entry *entry = queue.Pop();
      if (entry == nullptr) {
        break;
      }
      EXPECT_EQ(expected[n], entry);
      --n;
    }
    EXPECT_EQ(-1, n);
  }

  {
    UserHistoryPredictor::EntryPriorityQueue queue;
    for (int i = 0; i < 5; ++i) {
      UserHistoryPredictor::Entry *entry = queue.NewEntry();
      entry->set_key("test");
      entry->set_value("test");
      queue.Push(entry);
    }
    EXPECT_EQ(1, queue.size());

    for (int i = 0; i < 5; ++i) {
      UserHistoryPredictor::Entry *entry = queue.NewEntry();
      entry->set_key("foo");
      entry->set_value("bar");
      queue.Push(entry);
    }

    EXPECT_EQ(2, queue.size());
  }
}

namespace {

std::string RemoveLastUcs4Character(const std::string &input) {
  const size_t ucs4_count = Util::CharsLen(input);
  if (ucs4_count == 0) {
    return "";
  }

  size_t ucs4_processed = 0;
  std::string output;
  for (ConstChar32Iterator iter(input);
       !iter.Done() && (ucs4_processed < ucs4_count - 1);
       iter.Next(), ++ucs4_processed) {
    Util::Ucs4ToUtf8Append(iter.Get(), &output);
  }
  return output;
}

struct PrivacySensitiveTestData {
  bool is_sensitive;
  const char *scenario_description;
  const char *input;
  const char *output;
};

constexpr bool kSensitive = true;
constexpr bool kNonSensitive = false;

const PrivacySensitiveTestData kNonSensitiveCases[] = {
    {kNonSensitive,  // We might want to revisit this behavior
     "Type privacy sensitive number but it is committed as full-width number "
     "by mistake.",
     "0007", "????????????"},
    {kNonSensitive, "Type a ZIP number.", "100-0001", "??????????????????????????????"},
    {kNonSensitive,  // We might want to revisit this behavior
     "Type privacy sensitive number but the result contains one or more "
     "non-ASCII character such as full-width dash.",
     "1111-1111", "1111???1111"},
    {kNonSensitive,  // We might want to revisit this behavior
     "User dictionary contains a credit card number.", "?????????????????????",
     "0000-0000-0000-0000"},
    {kNonSensitive,  // We might want to revisit this behavior
     "User dictionary contains a credit card number.", "?????????????????????",
     "0000000000000000"},
    {kNonSensitive,  // We might want to revisit this behavior
     "User dictionary contains privacy sensitive information.", "???????????????",
     "ywwz1sxm"},
    {kNonSensitive,  // We might want to revisit this behavior
     "Input privacy sensitive text by Roman-input mode by mistake and then "
     "hit F10 key to convert it to half-alphanumeric text. In this case "
     "we assume all the alphabetical characters are consumed by Roman-input "
     "rules.",
     "??????1???3???", "ia1bo3xu"},
    {kNonSensitive,
     "Katakana to English transliteration.",  // http://b/4394325
     "????????????", "Orange"},
    {kNonSensitive,
     "Input a very common English word which should be included in our "
     "system dictionary by Roman-input mode by mistake and "
     "then hit F10 key to convert it to half-alphanumeric text.",
     "????????????", "orange"},
    {
        kNonSensitive,
        "Input a password-like text.",
        "123abc!",
        "123abc!",
    },
    {kNonSensitive,
     "Input privacy sensitive text by Roman-input mode by mistake and then "
     "hit F10 key to convert it to half-alphanumeric text. In this case, "
     "there may remain one or more alphabetical characters, which have not "
     "been consumed by Roman-input rules.",
     "y???wz1sxm", "ywwz1sxm"},
    {kNonSensitive,
     "Type a very common English word all in lower case which should be "
     "included in our system dictionary without capitalization.",
     "variable", "variable"},
    {kNonSensitive,
     "Type a very common English word all in upper case whose lower case "
     "should be included in our system dictionary.",
     "VARIABLE", "VARIABLE"},
    {kNonSensitive,
     "Type a very common English word with capitalization whose lower case "
     "should be included in our system dictionary.",
     "Variable", "Variable"},
    {kNonSensitive,  // We might want to revisit this behavior
     "Type a very common English word with random capitalization, which "
     "should be treated as case SENSITIVE.",
     "vArIaBle", "vArIaBle"},
    {
        kNonSensitive,
        "Type an English word in lower case but only its upper case form is "
        "stored in dictionary.",
        "upper",
        "upper",
    },
    {kSensitive,  // We might want to revisit this behavior
     "Type just a number.", "2398402938402934", "2398402938402934"},
    {kNonSensitive,  // We might want to revisit this behavior
     "Type an common English word which might be included in our system "
     "dictionary with number postfix.",
     "Orange10000", "Orange10000"},
};

}  // namespace

TEST_F(UserHistoryPredictorTest, PrivacySensitiveTest) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictor();

  // Add those words to the mock dictionary that are assumed to exist in privacy
  // sensitive filtering.
  const char *kEnglishWords[] = {
      "variable",
      "UPPER",
  };
  for (size_t i = 0; i < std::size(kEnglishWords); ++i) {
    // LookupPredictive is used in UserHistoryPredictor::IsPrivacySensitive().
    GetDictionaryMock()->AddLookupExact(kEnglishWords[i], kEnglishWords[i],
                                        kEnglishWords[i], Token::NONE);
  }

  for (size_t i = 0; i < std::size(kNonSensitiveCases); ++i) {
    predictor->ClearAllHistory();
    WaitForSyncer(predictor);

    const PrivacySensitiveTestData &data = kNonSensitiveCases[i];
    const std::string description(data.scenario_description);
    const std::string input(data.input);
    const std::string output(data.output);
    const std::string &partial_input = RemoveLastUcs4Character(input);
    const bool expect_sensitive = data.is_sensitive;

    // Initial commit.
    {
      Segments segments;
      SetUpInputForConversion(input, composer_.get(), &segments);
      AddCandidate(0, output, &segments);
      predictor->Finish(*convreq_, &segments);
    }

    // TODO(yukawa): Refactor the scenario runner below by making
    //     some utility functions.

    // Check suggestion
    {
      Segments segments;
      SetUpInputForSuggestion(partial_input, composer_.get(), &segments);
      if (expect_sensitive) {
        EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments))
            << description << " input: " << input << " output: " << output;
      } else {
        EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments))
            << description << " input: " << input << " output: " << output;
      }
      segments.Clear();
      SetUpInputForPrediction(input, composer_.get(), &segments);
      if (expect_sensitive) {
        EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments))
            << description << " input: " << input << " output: " << output;
      } else {
        EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments))
            << description << " input: " << input << " output: " << output;
      }
    }

    // Check Prediction
    {
      Segments segments;
      SetUpInputForPrediction(partial_input, composer_.get(), &segments);
      if (expect_sensitive) {
        EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments))
            << description << " input: " << input << " output: " << output;
      } else {
        EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments))
            << description << " input: " << input << " output: " << output;
      }
      segments.Clear();
      SetUpInputForPrediction(input, composer_.get(), &segments);
      if (expect_sensitive) {
        EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments))
            << description << " input: " << input << " output: " << output;
      } else {
        EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments))
            << description << " input: " << input << " output: " << output;
      }
    }
  }
}

TEST_F(UserHistoryPredictorTest, PrivacySensitiveMultiSegmentsTest) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictor();
  WaitForSyncer(predictor);

  // If a password-like input consists of multiple segments, it is not
  // considered to be privacy sensitive when the input is committed.
  // Currently this is a known issue.
  {
    Segments segments;
    SetUpInputForConversion("123", composer_.get(), &segments);
    AddSegmentForConversion("abc!", &segments);
    AddCandidate(0, "123", &segments);
    AddCandidate(1, "abc!", &segments);
    predictor->Finish(*convreq_, &segments);
  }

  {
    Segments segments;
    SetUpInputForSuggestion("123abc", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    segments.Clear();
    SetUpInputForSuggestion("123abc!", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  }

  {
    Segments segments;
    SetUpInputForPrediction("123abc", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    segments.Clear();
    SetUpInputForPrediction("123abc!", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  }
}

TEST_F(UserHistoryPredictorTest, UserHistoryStorage) {
  const std::string filename =
      FileUtil::JoinPath(SystemUtil::GetUserProfileDirectory(), "test");

  UserHistoryStorage storage1(filename);

  UserHistoryPredictor::Entry *entry = storage1.GetProto().add_entries();
  CHECK(entry);
  entry->set_key("key");
  entry->set_key("value");
  storage1.Save();
  UserHistoryStorage storage2(filename);
  storage2.Load();

  EXPECT_EQ(storage1.GetProto().DebugString(),
            storage2.GetProto().DebugString());
  EXPECT_OK(FileUtil::UnlinkIfExists(filename));
}

TEST_F(UserHistoryPredictorTest, UserHistoryStorageContainingOldEntries) {
  ScopedClockMock clock(1, 0);

  // Create a history proto containing old entries (timestamp = 1).
  user_history_predictor::UserHistory history;
  for (int i = 0; i < 10; ++i) {
    auto *entry = history.add_entries();
    entry->set_key(absl::StrFormat("old_key%d", i));
    entry->set_value(absl::StrFormat("old_value%d", i));
    entry->set_last_access_time(clock->GetTime());
  }
  clock->PutClockForward(63 * 24 * 60 * 60, 0);  // Advance clock for 63 days.
  for (int i = 0; i < 10; ++i) {
    auto *entry = history.add_entries();
    entry->set_key(absl::StrFormat("new_key%d", i));
    entry->set_value(absl::StrFormat("new_value%d", i));
    entry->set_last_access_time(clock->GetTime());
  }

  // Test Load().
  {
    const std::string filename =
        FileUtil::JoinPath(absl::GetFlag(FLAGS_test_tmpdir), "testload");
    // Write directly to the file to keep old entries for testing.
    storage::EncryptedStringStorage file_storage(filename);
    ASSERT_TRUE(file_storage.Save(history.SerializeAsString()));

    UserHistoryStorage storage(filename);
    ASSERT_TRUE(storage.Load());
    // Only the new entries are loaded.
    EXPECT_EQ(10, storage.GetProto().entries_size());
    for (const auto &entry : storage.GetProto().entries()) {
      EXPECT_TRUE(absl::StartsWith(entry.key(), "new_"));
      EXPECT_TRUE(absl::StartsWith(entry.value(), "new_"));
    }
    EXPECT_OK(FileUtil::Unlink(filename));
  }

  // Test Save().
  {
    const std::string filename =
        FileUtil::JoinPath(absl::GetFlag(FLAGS_test_tmpdir), "testsave");
    UserHistoryStorage storage(filename);
    storage.GetProto() = history;
    ASSERT_TRUE(storage.Save());

    // Directly open the file to check the actual entries written.
    storage::EncryptedStringStorage file_storage(filename);
    std::string content;
    ASSERT_TRUE(file_storage.Load(&content));
    user_history_predictor::UserHistory modified_history;
    ASSERT_TRUE(modified_history.ParseFromString(content));
    EXPECT_EQ(10, modified_history.entries_size());
    for (const auto &entry : storage.GetProto().entries()) {
      EXPECT_TRUE(absl::StartsWith(entry.key(), "new_"));
      EXPECT_TRUE(absl::StartsWith(entry.value(), "new_"));
    }
    EXPECT_OK(FileUtil::Unlink(filename));
  }
}

TEST_F(UserHistoryPredictorTest, UserHistoryStorageContainingInvalidEntries) {
  // This test checks invalid entries are not loaded into dic_.
  ScopedClockMock clock(1, 0);

  // Create a history proto containing invalid entries (timestamp = 1).
  user_history_predictor::UserHistory history;

  // Invalid UTF8.
  for (const char *value : {
           "\xC2\xC2 ",
           "\xE0\xE0\xE0 ",
           "\xF0\xF0\xF0\xF0 ",
           "\xFF ",
           "\xFE ",
           "\xC0\xAF",
           "\xE0\x80\xAF",
           // Real-world examples from b/116826494.
           "\xEF",
           "\xBC\x91\xE5",
       }) {
    auto *entry = history.add_entries();
    entry->set_key("key");
    entry->set_value(value);
  }

  // Test Load().
  {
    const std::string filename =
        FileUtil::JoinPath(absl::GetFlag(FLAGS_test_tmpdir), "testload");
    // Write directly to the file to keep invalid entries for testing.
    storage::EncryptedStringStorage file_storage(filename);
    ASSERT_TRUE(file_storage.Save(history.SerializeAsString()));
    FileUnlinker unlinker(filename);

    UserHistoryStorage storage(filename);
    ASSERT_TRUE(storage.Load());

    UserHistoryPredictor *predictor = GetUserHistoryPredictor();
    EXPECT_TRUE(LoadStorage(predictor, storage));

    // Only the valid entries are loaded.
    EXPECT_EQ(9, storage.GetProto().entries_size());
    EXPECT_EQ(0, EntrySize(*predictor));
  }
}

TEST_F(UserHistoryPredictorTest, RomanFuzzyPrefixMatch) {
  // same
  EXPECT_FALSE(UserHistoryPredictor::RomanFuzzyPrefixMatch("abc", "abc"));
  EXPECT_FALSE(UserHistoryPredictor::RomanFuzzyPrefixMatch("a", "a"));

  // exact prefix
  EXPECT_FALSE(UserHistoryPredictor::RomanFuzzyPrefixMatch("abc", "a"));
  EXPECT_FALSE(UserHistoryPredictor::RomanFuzzyPrefixMatch("abc", "ab"));
  EXPECT_FALSE(UserHistoryPredictor::RomanFuzzyPrefixMatch("abc", ""));

  // swap
  EXPECT_TRUE(UserHistoryPredictor::RomanFuzzyPrefixMatch("ab", "ba"));
  EXPECT_TRUE(UserHistoryPredictor::RomanFuzzyPrefixMatch("abfoo", "bafoo"));
  EXPECT_TRUE(UserHistoryPredictor::RomanFuzzyPrefixMatch("fooab", "fooba"));
  EXPECT_TRUE(
      UserHistoryPredictor::RomanFuzzyPrefixMatch("fooabfoo", "foobafoo"));

  // swap + prefix
  EXPECT_TRUE(UserHistoryPredictor::RomanFuzzyPrefixMatch("fooabfoo", "fooba"));

  // deletion
  EXPECT_TRUE(UserHistoryPredictor::RomanFuzzyPrefixMatch("abcd", "acd"));
  EXPECT_TRUE(UserHistoryPredictor::RomanFuzzyPrefixMatch("abcd", "bcd"));

  // deletion + prefix
  EXPECT_TRUE(UserHistoryPredictor::RomanFuzzyPrefixMatch("abcdf", "acd"));
  EXPECT_TRUE(UserHistoryPredictor::RomanFuzzyPrefixMatch("abcdfoo", "bcd"));

  // voice sound mark
  EXPECT_TRUE(
      UserHistoryPredictor::RomanFuzzyPrefixMatch("gu-guru", "gu^guru"));
  EXPECT_TRUE(
      UserHistoryPredictor::RomanFuzzyPrefixMatch("gu-guru", "gu=guru"));
  EXPECT_TRUE(UserHistoryPredictor::RomanFuzzyPrefixMatch("gu-guru", "gu^gu"));
  EXPECT_FALSE(UserHistoryPredictor::RomanFuzzyPrefixMatch("gu-guru", "gugu"));

  // Invalid
  EXPECT_FALSE(UserHistoryPredictor::RomanFuzzyPrefixMatch("", ""));
  EXPECT_FALSE(UserHistoryPredictor::RomanFuzzyPrefixMatch("", "a"));
  EXPECT_FALSE(UserHistoryPredictor::RomanFuzzyPrefixMatch("abcde", "defe"));
}

TEST_F(UserHistoryPredictorTest, MaybeRomanMisspelledKey) {
  EXPECT_TRUE(UserHistoryPredictor::MaybeRomanMisspelledKey("??????????????????"));
  EXPECT_TRUE(UserHistoryPredictor::MaybeRomanMisspelledKey("???????????????t"));
  EXPECT_FALSE(UserHistoryPredictor::MaybeRomanMisspelledKey("??????????????????"));
  EXPECT_TRUE(UserHistoryPredictor::MaybeRomanMisspelledKey("????????????"));
  EXPECT_FALSE(UserHistoryPredictor::MaybeRomanMisspelledKey("????????????"));
  EXPECT_TRUE(
      UserHistoryPredictor::MaybeRomanMisspelledKey("????????????????????????"));
  EXPECT_FALSE(UserHistoryPredictor::MaybeRomanMisspelledKey("?????????????????????"));
  EXPECT_TRUE(UserHistoryPredictor::MaybeRomanMisspelledKey("?????????=?????????"));
  EXPECT_FALSE(UserHistoryPredictor::MaybeRomanMisspelledKey("???"));
  EXPECT_TRUE(UserHistoryPredictor::MaybeRomanMisspelledKey("??????"));
  EXPECT_FALSE(
      UserHistoryPredictor::MaybeRomanMisspelledKey("????????????????????????"));
  // Two unknowns
  EXPECT_FALSE(
      UserHistoryPredictor::MaybeRomanMisspelledKey("????????????????????????"));
  // One alpha and one unknown
  EXPECT_FALSE(
      UserHistoryPredictor::MaybeRomanMisspelledKey("????????????????????????"));
}

TEST_F(UserHistoryPredictorTest, GetRomanMisspelledKey) {
  Segments segments;
  Segment *seg = segments.add_segment();
  seg->set_segment_type(Segment::FREE);
  Segment::Candidate *candidate = seg->add_candidate();
  candidate->value = "test";

  config_->set_preedit_method(config::Config::ROMAN);

  seg->set_key("");
  EXPECT_EQ("",
            UserHistoryPredictor::GetRomanMisspelledKey(*convreq_, segments));

  seg->set_key("?????????????????????s");
  EXPECT_EQ("onegaisimaus",
            UserHistoryPredictor::GetRomanMisspelledKey(*convreq_, segments));

  seg->set_key("?????????????????????");
  EXPECT_EQ("",
            UserHistoryPredictor::GetRomanMisspelledKey(*convreq_, segments));

  config_->set_preedit_method(config::Config::KANA);

  seg->set_key("?????????????????????s");
  EXPECT_EQ("",
            UserHistoryPredictor::GetRomanMisspelledKey(*convreq_, segments));

  seg->set_key("?????????????????????");
  EXPECT_EQ("",
            UserHistoryPredictor::GetRomanMisspelledKey(*convreq_, segments));
}

TEST_F(UserHistoryPredictorTest, RomanFuzzyLookupEntry) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictor();
  UserHistoryPredictor::Entry entry;
  UserHistoryPredictor::EntryPriorityQueue results;

  entry.set_key("");
  EXPECT_FALSE(predictor->RomanFuzzyLookupEntry("", &entry, &results));

  entry.set_key("????????????");
  EXPECT_TRUE(predictor->RomanFuzzyLookupEntry("yorosku", &entry, &results));
  EXPECT_TRUE(predictor->RomanFuzzyLookupEntry("yrosiku", &entry, &results));
  EXPECT_TRUE(predictor->RomanFuzzyLookupEntry("yorsiku", &entry, &results));
  EXPECT_FALSE(predictor->RomanFuzzyLookupEntry("yrsk", &entry, &results));
  EXPECT_FALSE(predictor->RomanFuzzyLookupEntry("yorosiku", &entry, &results));

  entry.set_key("????????????");
  EXPECT_TRUE(predictor->RomanFuzzyLookupEntry("gu=guru", &entry, &results));
  EXPECT_FALSE(predictor->RomanFuzzyLookupEntry("gu-guru", &entry, &results));
  EXPECT_FALSE(predictor->RomanFuzzyLookupEntry("g=guru", &entry, &results));
}

namespace {
struct LookupTestData {
  const std::string entry_key;
  const bool expect_result;
};
}  // namespace

TEST_F(UserHistoryPredictorTest, ExpandedLookupRoman) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictor();
  UserHistoryPredictor::Entry entry;
  UserHistoryPredictor::EntryPriorityQueue results;

  // Roman
  // preedit: "??????"
  // input_key: "??????"
  // key_base: "???"
  // key_expanded: "???","???","???","???", "???"
  std::unique_ptr<Trie<std::string>> expanded(new Trie<std::string>);
  expanded->AddEntry("???", "");
  expanded->AddEntry("???", "");
  expanded->AddEntry("???", "");
  expanded->AddEntry("???", "");
  expanded->AddEntry("???", "");

  const LookupTestData kTests1[] = {
      {"", false},       {"??????", true},    {"??????", true},  {"?????????", true},
      {"?????????", false}, {"???", false},     {"??????", false}, {"??????", false},
      {"?????????", false}, {"?????????", false}, {"???", false},
  };

  // with expanded
  for (size_t i = 0; i < std::size(kTests1); ++i) {
    entry.set_key(kTests1[i].entry_key);
    EXPECT_EQ(
        kTests1[i].expect_result,
        predictor->LookupEntry(UserHistoryPredictor::DEFAULT, "??????", "???",
                               expanded.get(), &entry, nullptr, &results))
        << kTests1[i].entry_key;
  }

  // only expanded
  // preedit: "k"
  // input_key: ""
  // key_base: ""
  // key_expanded: "???","???","???","???", "???"

  const LookupTestData kTests2[] = {
      {"", false},    {"???", true},    {"???", true},
      {"??????", true}, {"??????", false}, {"???", false},
  };

  for (size_t i = 0; i < std::size(kTests2); ++i) {
    entry.set_key(kTests2[i].entry_key);
    EXPECT_EQ(kTests2[i].expect_result,
              predictor->LookupEntry(UserHistoryPredictor::DEFAULT, "", "",
                                     expanded.get(), &entry, nullptr, &results))
        << kTests2[i].entry_key;
  }
}

TEST_F(UserHistoryPredictorTest, ExpandedLookupKana) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictor();
  UserHistoryPredictor::Entry entry;
  UserHistoryPredictor::EntryPriorityQueue results;

  // Kana
  // preedit: "??????"
  // input_key: "??????"
  // key_base: "???"
  // key_expanded: "???","???"
  std::unique_ptr<Trie<std::string>> expanded(new Trie<std::string>);
  expanded->AddEntry("???", "");
  expanded->AddEntry("???", "");

  const LookupTestData kTests1[] = {
      {"", false},           {"???", false},         {"??????", true},
      {"??????", true},        {"???????????????", true},  {"???????????????", true},
      {"???????????????", false}, {"??????", false},       {"??????", false},
      {"??????", false},       {"???????????????", false}, {"???????????????", false},
      {"???????????????", false}, {"??????", false},
  };

  // with expanded
  for (size_t i = 0; i < std::size(kTests1); ++i) {
    entry.set_key(kTests1[i].entry_key);
    EXPECT_EQ(
        kTests1[i].expect_result,
        predictor->LookupEntry(UserHistoryPredictor::DEFAULT, "??????", "???",
                               expanded.get(), &entry, nullptr, &results))
        << kTests1[i].entry_key;
  }

  // only expanded
  // input_key: "???"
  // key_base: ""
  // key_expanded: "???","???"
  const LookupTestData kTests2[] = {
      {"", false},          {"???", true},         {"???", true},
      {"???????????????", true}, {"???????????????", true}, {"???", false},
      {"??????", false},
  };

  for (size_t i = 0; i < std::size(kTests2); ++i) {
    entry.set_key(kTests2[i].entry_key);
    EXPECT_EQ(kTests2[i].expect_result,
              predictor->LookupEntry(UserHistoryPredictor::DEFAULT, "???", "",
                                     expanded.get(), &entry, nullptr, &results))
        << kTests2[i].entry_key;
  }
}

TEST_F(UserHistoryPredictorTest, GetMatchTypeFromInputRoman) {
  // We have to define this here,
  // because UserHistoryPredictor::MatchType is private
  struct MatchTypeTestData {
    const std::string target;
    const UserHistoryPredictor::MatchType expect_type;
  };

  // Roman
  // preedit: "??????"
  // input_key: "???"
  // key_base: "???"
  // key_expanded: "???","???","???","???", "???"
  std::unique_ptr<Trie<std::string>> expanded(new Trie<std::string>);
  expanded->AddEntry("???", "???");
  expanded->AddEntry("???", "???");
  expanded->AddEntry("???", "???");
  expanded->AddEntry("???", "???");
  expanded->AddEntry("???", "???");

  const MatchTypeTestData kTests1[] = {
      {"", UserHistoryPredictor::NO_MATCH},
      {"???", UserHistoryPredictor::NO_MATCH},
      {"???", UserHistoryPredictor::RIGHT_PREFIX_MATCH},
      {"??????", UserHistoryPredictor::NO_MATCH},
      {"??????", UserHistoryPredictor::LEFT_PREFIX_MATCH},
      {"?????????", UserHistoryPredictor::LEFT_PREFIX_MATCH},
  };

  for (size_t i = 0; i < std::size(kTests1); ++i) {
    EXPECT_EQ(kTests1[i].expect_type,
              UserHistoryPredictor::GetMatchTypeFromInput(
                  "???", "???", expanded.get(), kTests1[i].target))
        << kTests1[i].target;
  }

  // only expanded
  // preedit: "???"
  // input_key: ""
  // key_base: ""
  // key_expanded: "???","???","???","???", "???"
  const MatchTypeTestData kTests2[] = {
      {"", UserHistoryPredictor::NO_MATCH},
      {"???", UserHistoryPredictor::NO_MATCH},
      {"??????", UserHistoryPredictor::NO_MATCH},
      {"???", UserHistoryPredictor::LEFT_PREFIX_MATCH},
      {"????????????", UserHistoryPredictor::LEFT_PREFIX_MATCH},
  };

  for (size_t i = 0; i < std::size(kTests2); ++i) {
    EXPECT_EQ(kTests2[i].expect_type,
              UserHistoryPredictor::GetMatchTypeFromInput(
                  "", "", expanded.get(), kTests2[i].target))
        << kTests2[i].target;
  }
}

TEST_F(UserHistoryPredictorTest, GetMatchTypeFromInputKana) {
  // We have to define this here,
  // because UserHistoryPredictor::MatchType is private
  struct MatchTypeTestData {
    const std::string target;
    const UserHistoryPredictor::MatchType expect_type;
  };

  // Kana
  // preedit: "??????"
  // input_key: "??????"
  // key_base: "???"
  // key_expanded: "???","???"
  std::unique_ptr<Trie<std::string>> expanded(new Trie<std::string>);
  expanded->AddEntry("???", "???");
  expanded->AddEntry("???", "???");

  const MatchTypeTestData kTests1[] = {
      {"", UserHistoryPredictor::NO_MATCH},
      {"???", UserHistoryPredictor::NO_MATCH},
      {"??????", UserHistoryPredictor::NO_MATCH},
      {"???", UserHistoryPredictor::RIGHT_PREFIX_MATCH},
      {"??????", UserHistoryPredictor::EXACT_MATCH},
      {"??????", UserHistoryPredictor::LEFT_PREFIX_MATCH},
      {"?????????", UserHistoryPredictor::LEFT_PREFIX_MATCH},
      {"????????????", UserHistoryPredictor::LEFT_PREFIX_MATCH},
  };

  for (size_t i = 0; i < std::size(kTests1); ++i) {
    EXPECT_EQ(kTests1[i].expect_type,
              UserHistoryPredictor::GetMatchTypeFromInput(
                  "??????", "???", expanded.get(), kTests1[i].target))
        << kTests1[i].target;
  }

  // only expanded
  // preedit: "???"
  // input_key: "???"
  // key_base: ""
  // key_expanded: "???","???"
  const MatchTypeTestData kTests2[] = {
      {"", UserHistoryPredictor::NO_MATCH},
      {"???", UserHistoryPredictor::NO_MATCH},
      {"???", UserHistoryPredictor::EXACT_MATCH},
      {"???", UserHistoryPredictor::LEFT_PREFIX_MATCH},
      {"?????????", UserHistoryPredictor::LEFT_PREFIX_MATCH},
      {"?????????", UserHistoryPredictor::LEFT_PREFIX_MATCH},
  };

  for (size_t i = 0; i < std::size(kTests2); ++i) {
    EXPECT_EQ(kTests2[i].expect_type,
              UserHistoryPredictor::GetMatchTypeFromInput(
                  "???", "", expanded.get(), kTests2[i].target))
        << kTests2[i].target;
  }
}

namespace {
void InitSegmentsFromInputSequence(const std::string &text,
                                   composer::Composer *composer,
                                   ConversionRequest *request,
                                   Segments *segments) {
  DCHECK(composer);
  DCHECK(request);
  DCHECK(segments);
  const char *begin = text.data();
  const char *end = text.data() + text.size();
  size_t mblen = 0;

  while (begin < end) {
    commands::KeyEvent key;
    const char32_t w = Util::Utf8ToUcs4(begin, end, &mblen);
    if (w <= 0x7F) {  // IsAscii, w is unsigned.
      key.set_key_code(*begin);
    } else {
      key.set_key_code('?');
      key.set_key_string(std::string(begin, mblen));
    }
    begin += mblen;
    composer->InsertCharacterKeyEvent(key);
  }

  request->set_composer(composer);

  request->set_request_type(ConversionRequest::PREDICTION);
  Segment *segment = segments->add_segment();
  CHECK(segment);
  std::string query;
  composer->GetQueryForPrediction(&query);
  segment->set_key(query);
}
}  // namespace

TEST_F(UserHistoryPredictorTest, GetInputKeyFromSegmentsRoman) {
  table_->LoadFromFile("system://romanji-hiragana.tsv");
  composer_->SetTable(table_.get());
  Segments segments;

  InitSegmentsFromInputSequence("gu-g", composer_.get(), convreq_.get(),
                                &segments);
  std::string input_key;
  std::string base;
  std::unique_ptr<Trie<std::string>> expanded;
  UserHistoryPredictor::GetInputKeyFromSegments(*convreq_, segments, &input_key,
                                                &base, &expanded);
  EXPECT_EQ("?????????", input_key);
  EXPECT_EQ("??????", base);
  EXPECT_TRUE(expanded != nullptr);
  std::string value;
  size_t key_length = 0;
  bool has_subtrie = false;
  EXPECT_TRUE(expanded->LookUpPrefix("???", &value, &key_length, &has_subtrie));
  EXPECT_EQ("???", value);
}

namespace {
uint32_t GetRandomAscii() {
  return static_cast<uint32_t>(' ') +
         Util::Random(static_cast<uint32_t>('~' - ' '));
}
}  // namespace

TEST_F(UserHistoryPredictorTest, GetInputKeyFromSegmentsRomanRandom) {
  table_->LoadFromFile("system://romanji-hiragana.tsv");
  composer_->SetTable(table_.get());
  Segments segments;

  for (size_t i = 0; i < 1000; ++i) {
    composer_->Reset();
    const int len = 1 + Util::Random(4);
    DCHECK_GE(len, 1);
    DCHECK_LE(len, 5);
    std::string input;
    for (size_t j = 0; j < len; ++j) {
      input += GetRandomAscii();
    }
    InitSegmentsFromInputSequence(input, composer_.get(), convreq_.get(),
                                  &segments);
    std::string input_key;
    std::string base;
    std::unique_ptr<Trie<std::string>> expanded;
    UserHistoryPredictor::GetInputKeyFromSegments(*convreq_, segments,
                                                  &input_key, &base, &expanded);
  }
}

// Found by random test.
// input_key != base by compoesr modification.
TEST_F(UserHistoryPredictorTest, GetInputKeyFromSegmentsShouldNotCrash) {
  table_->LoadFromFile("system://romanji-hiragana.tsv");
  composer_->SetTable(table_.get());
  Segments segments;

  {
    InitSegmentsFromInputSequence("8,+", composer_.get(), convreq_.get(),
                                  &segments);
    std::string input_key;
    std::string base;
    std::unique_ptr<Trie<std::string>> expanded;
    UserHistoryPredictor::GetInputKeyFromSegments(*convreq_, segments,
                                                  &input_key, &base, &expanded);
  }
}

TEST_F(UserHistoryPredictorTest, GetInputKeyFromSegmentsRomanN) {
  table_->LoadFromFile("system://romanji-hiragana.tsv");
  composer_->SetTable(table_.get());
  Segments segments;

  {
    InitSegmentsFromInputSequence("n", composer_.get(), convreq_.get(),
                                  &segments);
    std::string input_key;
    std::string base;
    std::unique_ptr<Trie<std::string>> expanded;
    UserHistoryPredictor::GetInputKeyFromSegments(*convreq_, segments,
                                                  &input_key, &base, &expanded);
    EXPECT_EQ("???", input_key);
    EXPECT_EQ("", base);
    EXPECT_TRUE(expanded != nullptr);
    std::string value;
    size_t key_length = 0;
    bool has_subtrie = false;
    EXPECT_TRUE(
        expanded->LookUpPrefix("???", &value, &key_length, &has_subtrie));
    EXPECT_EQ("???", value);
  }

  composer_->Reset();
  segments.Clear();
  {
    InitSegmentsFromInputSequence("nn", composer_.get(), convreq_.get(),
                                  &segments);
    std::string input_key;
    std::string base;
    std::unique_ptr<Trie<std::string>> expanded;
    UserHistoryPredictor::GetInputKeyFromSegments(*convreq_, segments,
                                                  &input_key, &base, &expanded);
    EXPECT_EQ("???", input_key);
    EXPECT_EQ("???", base);
    EXPECT_TRUE(expanded == nullptr);
  }

  composer_->Reset();
  segments.Clear();
  {
    InitSegmentsFromInputSequence("n'", composer_.get(), convreq_.get(),
                                  &segments);
    std::string input_key;
    std::string base;
    std::unique_ptr<Trie<std::string>> expanded;
    UserHistoryPredictor::GetInputKeyFromSegments(*convreq_, segments,
                                                  &input_key, &base, &expanded);
    EXPECT_EQ("???", input_key);
    EXPECT_EQ("???", base);
    EXPECT_TRUE(expanded == nullptr);
  }

  composer_->Reset();
  segments.Clear();
  {
    InitSegmentsFromInputSequence("n'n", composer_.get(), convreq_.get(),
                                  &segments);
    std::string input_key;
    std::string base;
    std::unique_ptr<Trie<std::string>> expanded;
    UserHistoryPredictor::GetInputKeyFromSegments(*convreq_, segments,
                                                  &input_key, &base, &expanded);
    EXPECT_EQ("??????", input_key);
    EXPECT_EQ("???", base);
    EXPECT_TRUE(expanded != nullptr);
    std::string value;
    size_t key_length = 0;
    bool has_subtrie = false;
    EXPECT_TRUE(
        expanded->LookUpPrefix("???", &value, &key_length, &has_subtrie));
    EXPECT_EQ("???", value);
  }
}

TEST_F(UserHistoryPredictorTest, GetInputKeyFromSegmentsFlickN) {
  table_->LoadFromFile("system://flick-hiragana.tsv");
  composer_->SetTable(table_.get());
  Segments segments;

  {
    InitSegmentsFromInputSequence("/", composer_.get(), convreq_.get(),
                                  &segments);
    std::string input_key;
    std::string base;
    std::unique_ptr<Trie<std::string>> expanded;
    UserHistoryPredictor::GetInputKeyFromSegments(*convreq_, segments,
                                                  &input_key, &base, &expanded);
    EXPECT_EQ("???", input_key);
    EXPECT_EQ("", base);
    EXPECT_TRUE(expanded != nullptr);
    std::string value;
    size_t key_length = 0;
    bool has_subtrie = false;
    EXPECT_TRUE(
        expanded->LookUpPrefix("???", &value, &key_length, &has_subtrie));
    EXPECT_EQ("???", value);
  }
}

TEST_F(UserHistoryPredictorTest, GetInputKeyFromSegments12KeyN) {
  table_->LoadFromFile("system://12keys-hiragana.tsv");
  composer_->SetTable(table_.get());
  Segments segments;

  {
    InitSegmentsFromInputSequence("???00", composer_.get(), convreq_.get(),
                                  &segments);
    std::string input_key;
    std::string base;
    std::unique_ptr<Trie<std::string>> expanded;
    UserHistoryPredictor::GetInputKeyFromSegments(*convreq_, segments,
                                                  &input_key, &base, &expanded);
    EXPECT_EQ("???", input_key);
    EXPECT_EQ("", base);
    EXPECT_TRUE(expanded != nullptr);
    std::string value;
    size_t key_length = 0;
    bool has_subtrie = false;
    EXPECT_TRUE(
        expanded->LookUpPrefix("???", &value, &key_length, &has_subtrie));
    EXPECT_EQ("???", value);
  }
}

TEST_F(UserHistoryPredictorTest, GetInputKeyFromSegmentsKana) {
  table_->LoadFromFile("system://kana.tsv");
  composer_->SetTable(table_.get());
  Segments segments;

  InitSegmentsFromInputSequence("??????", composer_.get(), convreq_.get(),
                                &segments);

  {
    std::string input_key;
    std::string base;
    std::unique_ptr<Trie<std::string>> expanded;
    UserHistoryPredictor::GetInputKeyFromSegments(*convreq_, segments,
                                                  &input_key, &base, &expanded);
    EXPECT_EQ("??????", input_key);
    EXPECT_EQ("???", base);
    EXPECT_TRUE(expanded != nullptr);
    std::string value;
    size_t key_length = 0;
    bool has_subtrie = false;
    EXPECT_TRUE(
        expanded->LookUpPrefix("???", &value, &key_length, &has_subtrie));
    EXPECT_EQ("???", value);
  }
}

TEST_F(UserHistoryPredictorTest, RealtimeConversionInnerSegment) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;
  {
    constexpr char kKey[] = "???????????????????????????????????????";
    constexpr char kValue[] = "???????????????????????????";
    SetUpInputForPrediction(kKey, composer_.get(), &segments);
    Segment::Candidate *candidate =
        segments.mutable_segment(0)->add_candidate();
    CHECK(candidate);
    candidate->Init();
    candidate->value = kValue;
    candidate->content_value = kValue;
    candidate->key = kKey;
    candidate->content_key = kKey;
    // "????????????, ??????", "?????????, ???"
    candidate->PushBackInnerSegmentBoundary(12, 6, 9, 3);
    // "????????????, ?????????", "?????????, ??????"
    candidate->PushBackInnerSegmentBoundary(12, 9, 9, 6);
    // "???????????????, ????????????", "?????????, ??????"
    candidate->PushBackInnerSegmentBoundary(15, 12, 9, 6);
  }
  predictor->Finish(*convreq_, &segments);
  segments.Clear();

  SetUpInputForPrediction("?????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_TRUE(FindCandidateByValue("????????????", segments));

  segments.Clear();
  SetUpInputForPrediction("?????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_TRUE(FindCandidateByValue("?????????", segments));
  EXPECT_TRUE(FindCandidateByValue("?????????????????????", segments));
}

TEST_F(UserHistoryPredictorTest, ZeroQueryFromRealtimeConversion) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;
  {
    constexpr char kKey[] = "???????????????????????????????????????";
    constexpr char kValue[] = "???????????????????????????";
    SetUpInputForPrediction(kKey, composer_.get(), &segments);
    Segment::Candidate *candidate =
        segments.mutable_segment(0)->add_candidate();
    CHECK(candidate);
    candidate->Init();
    candidate->value = kValue;
    candidate->content_value = kValue;
    candidate->key = kKey;
    candidate->content_key = kKey;
    // "????????????, ??????", "?????????, ???"
    candidate->PushBackInnerSegmentBoundary(12, 6, 9, 3);
    // "????????????, ?????????", "?????????, ??????"
    candidate->PushBackInnerSegmentBoundary(12, 9, 9, 6);
    // "???????????????, ????????????", "?????????, ??????"
    candidate->PushBackInnerSegmentBoundary(15, 12, 9, 6);
  }
  predictor->Finish(*convreq_, &segments);
  segments.Clear();

  SetUpInputForConversion("????????????", composer_.get(), &segments);
  AddCandidate(0, "??????", &segments);
  predictor->Finish(*convreq_, &segments);
  segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

  SetUpInputForSuggestionWithHistory("", "????????????", "??????", composer_.get(),
                                     &segments);
  commands::Request request;
  request_->set_zero_query_suggestion(true);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_TRUE(FindCandidateByValue("?????????", segments));
}

TEST_F(UserHistoryPredictorTest, LongCandidateForMobile) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  commands::RequestForUnitTest::FillMobileRequest(request_.get());

  Segments segments;
  for (size_t i = 0; i < 3; ++i) {
    constexpr char kKey[] = "?????????????????????????????????";
    constexpr char kValue[] = "??????????????????????????????";
    SetUpInputForPrediction(kKey, composer_.get(), &segments);
    Segment::Candidate *candidate =
        segments.mutable_segment(0)->add_candidate();
    CHECK(candidate);
    candidate->Init();
    candidate->value = kValue;
    candidate->content_value = kValue;
    candidate->key = kKey;
    candidate->content_key = kKey;
    predictor->Finish(*convreq_, &segments);
    segments.Clear();
  }

  SetUpInputForPrediction("??????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_TRUE(FindCandidateByValue("??????????????????????????????", segments));
}

TEST_F(UserHistoryPredictorTest, EraseNextEntries) {
  UserHistoryPredictor::Entry e;
  e.add_next_entries()->set_entry_fp(100);
  e.add_next_entries()->set_entry_fp(10);
  e.add_next_entries()->set_entry_fp(30);
  e.add_next_entries()->set_entry_fp(10);
  e.add_next_entries()->set_entry_fp(100);

  UserHistoryPredictor::EraseNextEntries(1234, &e);
  EXPECT_EQ(5, e.next_entries_size());

  UserHistoryPredictor::EraseNextEntries(30, &e);
  ASSERT_EQ(4, e.next_entries_size());
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_NE(30, e.next_entries(i).entry_fp());
  }

  UserHistoryPredictor::EraseNextEntries(10, &e);
  ASSERT_EQ(2, e.next_entries_size());
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_NE(10, e.next_entries(i).entry_fp());
  }

  UserHistoryPredictor::EraseNextEntries(100, &e);
  EXPECT_EQ(0, e.next_entries_size());
}

TEST_F(UserHistoryPredictorTest, RemoveNgramChain) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Set up the following chain of next entries:
  // ("abc", "ABC")
  // (  "a",   "A") --- ("b", "B") --- ("c", "C")
  UserHistoryPredictor::Entry *abc = InsertEntry(predictor, "abc", "ABC");
  UserHistoryPredictor::Entry *a = InsertEntry(predictor, "a", "A");
  UserHistoryPredictor::Entry *b = AppendEntry(predictor, "b", "B", a);
  UserHistoryPredictor::Entry *c = AppendEntry(predictor, "c", "C", b);

  std::vector<UserHistoryPredictor::Entry *> entries;
  entries.push_back(abc);
  entries.push_back(a);
  entries.push_back(b);
  entries.push_back(c);

  // The method should return NOT_FOUND for key-value pairs not in the chain.
  for (size_t i = 0; i < entries.size(); ++i) {
    std::vector<absl::string_view> dummy1, dummy2;
    EXPECT_EQ(UserHistoryPredictor::NOT_FOUND,
              predictor->RemoveNgramChain("hoge", "HOGE", entries[i], &dummy1,
                                          0, &dummy2, 0));
  }
  // Moreover, all nodes and links should be kept.
  for (size_t i = 0; i < entries.size(); ++i) {
    EXPECT_FALSE(entries[i]->removed());
  }
  EXPECT_TRUE(IsConnected(*a, *b));
  EXPECT_TRUE(IsConnected(*b, *c));

  {
    // Try deleting the chain for "abc". Only the link from "b" to "c" should be
    // removed.
    std::vector<absl::string_view> dummy1, dummy2;
    EXPECT_EQ(
        UserHistoryPredictor::DONE,
        predictor->RemoveNgramChain("abc", "ABC", a, &dummy1, 0, &dummy2, 0));
    for (size_t i = 0; i < entries.size(); ++i) {
      EXPECT_FALSE(entries[i]->removed());
    }
    EXPECT_TRUE(IsConnected(*a, *b));
    EXPECT_FALSE(IsConnected(*b, *c));
  }
  {
    // Try deleting the chain for "a". Since this is the head of the chain, the
    // function returns TAIL and nothing should be removed.
    std::vector<absl::string_view> dummy1, dummy2;
    EXPECT_EQ(UserHistoryPredictor::TAIL,
              predictor->RemoveNgramChain("a", "A", a, &dummy1, 0, &dummy2, 0));
    for (size_t i = 0; i < entries.size(); ++i) {
      EXPECT_FALSE(entries[i]->removed());
    }
    EXPECT_TRUE(IsConnected(*a, *b));
    EXPECT_FALSE(IsConnected(*b, *c));
  }
  {
    // Further delete the chain for "ab".  Now all the links should be removed.
    std::vector<absl::string_view> dummy1, dummy2;
    EXPECT_EQ(
        UserHistoryPredictor::DONE,
        predictor->RemoveNgramChain("ab", "AB", a, &dummy1, 0, &dummy2, 0));
    for (size_t i = 0; i < entries.size(); ++i) {
      EXPECT_FALSE(entries[i]->removed());
    }
    EXPECT_FALSE(IsConnected(*a, *b));
    EXPECT_FALSE(IsConnected(*b, *c));
  }
}

TEST_F(UserHistoryPredictorTest, ClearHistoryEntryUnigram) {
  ScopedClockMock clock(1, 0);

  // Tests ClearHistoryEntry() for unigram history.
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Add a unigram history ("japanese", "Japanese").
  UserHistoryPredictor::Entry *e =
      InsertEntry(predictor, "japanese", "Japanese");
  e->set_last_access_time(1);

  // "Japanese" should be suggested and predicted from "japan".
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));

  // Delete the history.
  EXPECT_TRUE(predictor->ClearHistoryEntry("japanese", "Japanese"));

  EXPECT_TRUE(e->removed());

  // "Japanese" should be never be suggested nor predicted.
  const std::string key = "japanese";
  for (size_t i = 0; i < key.size(); ++i) {
    const std::string &prefix = key.substr(0, i);
    EXPECT_FALSE(IsSuggested(predictor, prefix, "Japanese"));
    EXPECT_FALSE(IsPredicted(predictor, prefix, "Japanese"));
  }
}

TEST_F(UserHistoryPredictorTest, ClearHistoryEntryBigramDeleteWhole) {
  ScopedClockMock clock(1, 0);

  // Tests ClearHistoryEntry() for bigram history.  This case tests the deletion
  // of whole sentence.
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Make the history for ("japaneseinput", "JapaneseInput"). It's assumed that
  // this sentence consists of two segments, "japanese" and "input". So, the
  // following history entries are constructed:
  //   ("japaneseinput", "JapaneseInput")  // Unigram
  //   ("japanese", "Japanese") --- ("input", "Input")  // Bigram chain
  UserHistoryPredictor::Entry *japaneseinput;
  UserHistoryPredictor::Entry *japanese;
  UserHistoryPredictor::Entry *input;
  InitHistory_JapaneseInput(predictor, &japaneseinput, &japanese, &input);

  // Check the predictor functionality for the above history structure.
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "input", "Input"));

  // Delete the unigram ("japaneseinput", "JapaneseInput").
  EXPECT_TRUE(predictor->ClearHistoryEntry("japaneseinput", "JapaneseInput"));

  EXPECT_TRUE(japaneseinput->removed());
  EXPECT_FALSE(japanese->removed());
  EXPECT_FALSE(input->removed());
  EXPECT_FALSE(IsConnected(*japanese, *input));

  // Now "JapaneseInput" should never be suggested nor predicted.
  const std::string key = "japaneseinput";
  for (size_t i = 0; i < key.size(); ++i) {
    const std::string &prefix = key.substr(0, i);
    EXPECT_FALSE(IsSuggested(predictor, prefix, "Japaneseinput"));
    EXPECT_FALSE(IsPredicted(predictor, prefix, "Japaneseinput"));
  }

  // However, predictor should show "Japanese" and "Input".
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "Input"));
}

TEST_F(UserHistoryPredictorTest, ClearHistoryEntryBigramDeleteFirst) {
  ScopedClockMock clock(1, 0);

  // Tests ClearHistoryEntry() for bigram history.  This case tests the deletion
  // of the first node of the bigram chain.
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Make the history for ("japaneseinput", "JapaneseInput"), i.e., the same
  // history structure as ClearHistoryEntry_Bigram_DeleteWhole is constructed.
  UserHistoryPredictor::Entry *japaneseinput;
  UserHistoryPredictor::Entry *japanese;
  UserHistoryPredictor::Entry *input;
  InitHistory_JapaneseInput(predictor, &japaneseinput, &japanese, &input);

  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "input", "Input"));

  // Delete the first bigram node ("japanese", "Japanese").
  EXPECT_TRUE(predictor->ClearHistoryEntry("japanese", "Japanese"));

  // Note that the first node was removed but the connection to the second node
  // is still valid.
  EXPECT_FALSE(japaneseinput->removed());
  EXPECT_TRUE(japanese->removed());
  EXPECT_FALSE(input->removed());
  EXPECT_TRUE(IsConnected(*japanese, *input));

  // Now "Japanese" should never be suggested nor predicted.
  const std::string key = "japaneseinput";
  for (size_t i = 0; i < key.size(); ++i) {
    const std::string &prefix = key.substr(0, i);
    EXPECT_FALSE(IsSuggested(predictor, prefix, "Japanese"));
    EXPECT_FALSE(IsPredicted(predictor, prefix, "Japanese"));
  }

  // However, predictor should show "JapaneseInput" and "Input".
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "Input"));
}

TEST_F(UserHistoryPredictorTest, ClearHistoryEntryBigramDeleteSecond) {
  ScopedClockMock clock(1, 0);

  // Tests ClearHistoryEntry() for bigram history.  This case tests the deletion
  // of the first node of the bigram chain.
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Make the history for ("japaneseinput", "JapaneseInput"), i.e., the same
  // history structure as ClearHistoryEntry_Bigram_DeleteWhole is constructed.
  UserHistoryPredictor::Entry *japaneseinput;
  UserHistoryPredictor::Entry *japanese;
  UserHistoryPredictor::Entry *input;
  InitHistory_JapaneseInput(predictor, &japaneseinput, &japanese, &input);

  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "input", "Input"));

  // Delete the second bigram node ("input", "Input").
  EXPECT_TRUE(predictor->ClearHistoryEntry("input", "Input"));

  EXPECT_FALSE(japaneseinput->removed());
  EXPECT_FALSE(japanese->removed());
  EXPECT_TRUE(input->removed());
  EXPECT_TRUE(IsConnected(*japanese, *input));

  // Now "Input" should never be suggested nor predicted.
  const std::string key = "input";
  for (size_t i = 0; i < key.size(); ++i) {
    const std::string &prefix = key.substr(0, i);
    EXPECT_FALSE(IsSuggested(predictor, prefix, "Input"));
    EXPECT_FALSE(IsPredicted(predictor, prefix, "Input"));
  }

  // However, predictor should show "Japanese" and "JapaneseInput".
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
}

TEST_F(UserHistoryPredictorTest, ClearHistoryEntryTrigramDeleteWhole) {
  ScopedClockMock clock(1, 0);

  // Tests ClearHistoryEntry() for trigram history.  This case tests the
  // deletion of the whole sentence.
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Make the history for ("japaneseinputmethod", "JapaneseInputMethod"). It's
  // assumed that this sentence consists of three segments, "japanese", "input"
  // and "method". So, the following history entries are constructed:
  //   ("japaneseinputmethod", "JapaneseInputMethod")  // Unigram
  //   ("japanese", "Japanese") -- ("input", "Input") -- ("method", "Method")
  UserHistoryPredictor::Entry *japaneseinputmethod;
  UserHistoryPredictor::Entry *japanese;
  UserHistoryPredictor::Entry *input;
  UserHistoryPredictor::Entry *method;
  InitHistory_JapaneseInputMethod(predictor, &japaneseinputmethod, &japanese,
                                  &input, &method);

  // Delete the history of the whole sentence.
  EXPECT_TRUE(predictor->ClearHistoryEntry("japaneseinputmethod",
                                           "JapaneseInputMethod"));

  // Note that only the link from "input" to "method" was removed.
  EXPECT_TRUE(japaneseinputmethod->removed());
  EXPECT_FALSE(japanese->removed());
  EXPECT_FALSE(input->removed());
  EXPECT_FALSE(method->removed());
  EXPECT_TRUE(IsConnected(*japanese, *input));
  EXPECT_FALSE(IsConnected(*input, *method));

  {
    // Now "JapaneseInputMethod" should never be suggested nor predicted.
    const std::string key = "japaneseinputmethod";
    for (size_t i = 0; i < key.size(); ++i) {
      const std::string &prefix = key.substr(0, i);
      EXPECT_FALSE(IsSuggested(predictor, prefix, "JapaneseInputMethod"));
      EXPECT_FALSE(IsPredicted(predictor, prefix, "JapaneseInputMethod"));
    }
  }
  {
    // Here's a limitation of chain cut.  Since we have cut the link from
    // "input" to "method", the predictor cannot show "InputMethod" although it
    // could before.  However, since "InputMethod" is not the direct input by
    // the user (user's input was "JapaneseInputMethod" in this case), this
    // limitation would be acceptable.
    const std::string key = "inputmethod";
    for (size_t i = 0; i < key.size(); ++i) {
      const std::string &prefix = key.substr(0, i);
      EXPECT_FALSE(IsSuggested(predictor, prefix, "InputMethod"));
      EXPECT_FALSE(IsPredicted(predictor, prefix, "InputMethod"));
    }
  }

  // The following can be still suggested and predicted.
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "Input"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "meth", "Method"));
}

TEST_F(UserHistoryPredictorTest, ClearHistoryEntryTrigramDeleteFirst) {
  ScopedClockMock clock(1, 0);

  // Tests ClearHistoryEntry() for trigram history.  This case tests the
  // deletion of the first node of trigram.
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Make the same history structure as ClearHistoryEntry_Trigram_DeleteWhole.
  UserHistoryPredictor::Entry *japaneseinputmethod;
  UserHistoryPredictor::Entry *japanese;
  UserHistoryPredictor::Entry *input;
  UserHistoryPredictor::Entry *method;
  InitHistory_JapaneseInputMethod(predictor, &japaneseinputmethod, &japanese,
                                  &input, &method);

  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(
      IsSuggestedAndPredicted(predictor, "japan", "JapaneseInputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "Input"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "InputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "meth", "Method"));

  // Delete the first node of the chain.
  EXPECT_TRUE(predictor->ClearHistoryEntry("japanese", "Japanese"));

  // Note that the two links are still alive.
  EXPECT_FALSE(japaneseinputmethod->removed());
  EXPECT_TRUE(japanese->removed());
  EXPECT_FALSE(input->removed());
  EXPECT_FALSE(method->removed());
  EXPECT_TRUE(IsConnected(*japanese, *input));
  EXPECT_TRUE(IsConnected(*input, *method));

  {
    // Now "Japanese" should never be suggested nor predicted.
    const std::string key = "japaneseinputmethod";
    for (size_t i = 0; i < key.size(); ++i) {
      const std::string &prefix = key.substr(0, i);
      EXPECT_FALSE(IsSuggested(predictor, prefix, "Japanese"));
      EXPECT_FALSE(IsPredicted(predictor, prefix, "Japanese"));
    }
  }

  // The following are still suggested and predicted.
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(
      IsSuggestedAndPredicted(predictor, "japan", "JapaneseInputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "Input"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "InputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "meth", "Method"));
}

TEST_F(UserHistoryPredictorTest, ClearHistoryEntryTrigramDeleteSecond) {
  ScopedClockMock clock(1, 0);

  // Tests ClearHistoryEntry() for trigram history.  This case tests the
  // deletion of the second node of trigram.
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Make the same history structure as ClearHistoryEntry_Trigram_DeleteWhole.
  UserHistoryPredictor::Entry *japaneseinputmethod;
  UserHistoryPredictor::Entry *japanese;
  UserHistoryPredictor::Entry *input;
  UserHistoryPredictor::Entry *method;
  InitHistory_JapaneseInputMethod(predictor, &japaneseinputmethod, &japanese,
                                  &input, &method);

  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(
      IsSuggestedAndPredicted(predictor, "japan", "JapaneseInputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "Input"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "InputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "meth", "Method"));

  // Delete the second node of the chain.
  EXPECT_TRUE(predictor->ClearHistoryEntry("input", "Input"));

  // Note that the two links are still alive.
  EXPECT_FALSE(japaneseinputmethod->removed());
  EXPECT_FALSE(japanese->removed());
  EXPECT_TRUE(input->removed());
  EXPECT_FALSE(method->removed());
  EXPECT_TRUE(IsConnected(*japanese, *input));
  EXPECT_TRUE(IsConnected(*input, *method));

  {
    // Now "Input" should never be suggested nor predicted.
    const std::string key = "inputmethod";
    for (size_t i = 0; i < key.size(); ++i) {
      const std::string &prefix = key.substr(0, i);
      EXPECT_FALSE(IsSuggested(predictor, prefix, "Input"));
      EXPECT_FALSE(IsPredicted(predictor, prefix, "Input"));
    }
  }

  // The following can still be shown by the predictor.
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(
      IsSuggestedAndPredicted(predictor, "japan", "JapaneseInputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "InputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "meth", "Method"));
}

TEST_F(UserHistoryPredictorTest, ClearHistoryEntryTrigramDeleteThird) {
  ScopedClockMock clock(1, 0);

  // Tests ClearHistoryEntry() for trigram history.  This case tests the
  // deletion of the third node of trigram.
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Make the same history structure as ClearHistoryEntry_Trigram_DeleteWhole.
  UserHistoryPredictor::Entry *japaneseinputmethod;
  UserHistoryPredictor::Entry *japanese;
  UserHistoryPredictor::Entry *input;
  UserHistoryPredictor::Entry *method;
  InitHistory_JapaneseInputMethod(predictor, &japaneseinputmethod, &japanese,
                                  &input, &method);

  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(
      IsSuggestedAndPredicted(predictor, "japan", "JapaneseInputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "Input"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "InputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "meth", "Method"));

  // Delete the third node, "method".
  EXPECT_TRUE(predictor->ClearHistoryEntry("method", "Method"));

  // Note that the two links are still alive.
  EXPECT_FALSE(japaneseinputmethod->removed());
  EXPECT_FALSE(japanese->removed());
  EXPECT_FALSE(input->removed());
  EXPECT_TRUE(method->removed());
  EXPECT_TRUE(IsConnected(*japanese, *input));
  EXPECT_TRUE(IsConnected(*input, *method));

  {
    // Now "Method" should never be suggested nor predicted.
    const std::string key = "method";
    for (size_t i = 0; i < key.size(); ++i) {
      const std::string &prefix = key.substr(0, i);
      EXPECT_FALSE(IsSuggested(predictor, prefix, "Method"));
      EXPECT_FALSE(IsPredicted(predictor, prefix, "Method"));
    }
  }

  // The following can still be shown by the predictor.
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(
      IsSuggestedAndPredicted(predictor, "japan", "JapaneseInputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "Input"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "InputMethod"));
}

TEST_F(UserHistoryPredictorTest, ClearHistoryEntryTrigramDeleteFirstBigram) {
  ScopedClockMock clock(1, 0);

  // Tests ClearHistoryEntry() for trigram history.  This case tests the
  // deletion of the first bigram of trigram.
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Make the same history structure as ClearHistoryEntry_Trigram_DeleteWhole.
  UserHistoryPredictor::Entry *japaneseinputmethod;
  UserHistoryPredictor::Entry *japanese;
  UserHistoryPredictor::Entry *input;
  UserHistoryPredictor::Entry *method;
  InitHistory_JapaneseInputMethod(predictor, &japaneseinputmethod, &japanese,
                                  &input, &method);

  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(
      IsSuggestedAndPredicted(predictor, "japan", "JapaneseInputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "Input"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "InputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "meth", "Method"));

  // Delete the sentence consisting of the first two nodes.
  EXPECT_TRUE(predictor->ClearHistoryEntry("japaneseinput", "JapaneseInput"));

  // Note that the node "japaneseinput" and the link from "japanese" to "input"
  // were removed.
  EXPECT_FALSE(japaneseinputmethod->removed());
  EXPECT_FALSE(japanese->removed());
  EXPECT_FALSE(input->removed());
  EXPECT_FALSE(method->removed());
  EXPECT_FALSE(IsConnected(*japanese, *input));
  EXPECT_TRUE(IsConnected(*input, *method));

  {
    // Now "JapaneseInput" should never be suggested nor predicted.
    const std::string key = "japaneseinputmethod";
    for (size_t i = 0; i < key.size(); ++i) {
      const std::string &prefix = key.substr(0, i);
      EXPECT_FALSE(IsSuggested(predictor, prefix, "JapaneseInput"));
      EXPECT_FALSE(IsPredicted(predictor, prefix, "JapaneseInput"));
    }
  }

  // However, the following can still be available, including
  // "JapaneseInputMethod".
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(
      IsSuggestedAndPredicted(predictor, "japan", "JapaneseInputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "Input"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "InputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "meth", "Method"));
}

TEST_F(UserHistoryPredictorTest, ClearHistoryEntryTrigramDeleteSecondBigram) {
  ScopedClockMock clock(1, 0);

  // Tests ClearHistoryEntry() for trigram history.  This case tests the
  // deletion of the latter bigram of trigram.
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Make the same history structure as ClearHistoryEntry_Trigram_DeleteWhole.
  UserHistoryPredictor::Entry *japaneseinputmethod;
  UserHistoryPredictor::Entry *japanese;
  UserHistoryPredictor::Entry *input;
  UserHistoryPredictor::Entry *method;
  InitHistory_JapaneseInputMethod(predictor, &japaneseinputmethod, &japanese,
                                  &input, &method);

  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(
      IsSuggestedAndPredicted(predictor, "japan", "JapaneseInputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "Input"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "InputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "meth", "Method"));

  // Delete the latter bigram.
  EXPECT_TRUE(predictor->ClearHistoryEntry("inputmethod", "InputMethod"));

  // Note that only link from "input" to "method" was removed.
  EXPECT_FALSE(japaneseinputmethod->removed());
  EXPECT_FALSE(japanese->removed());
  EXPECT_FALSE(input->removed());
  EXPECT_FALSE(method->removed());
  EXPECT_TRUE(IsConnected(*japanese, *input));
  EXPECT_FALSE(IsConnected(*input, *method));

  {
    // Now "InputMethod" should never be suggested.
    const std::string key = "inputmethod";
    for (size_t i = 0; i < key.size(); ++i) {
      const std::string &prefix = key.substr(0, i);
      EXPECT_FALSE(IsSuggested(predictor, prefix, "InputMethod"));
      EXPECT_FALSE(IsPredicted(predictor, prefix, "InputMethod"));
    }
  }

  // However, the following are available.
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "Japanese"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "japan", "JapaneseInput"));
  EXPECT_TRUE(
      IsSuggestedAndPredicted(predictor, "japan", "JapaneseInputMethod"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "inpu", "Input"));
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "meth", "Method"));
}

TEST_F(UserHistoryPredictorTest, ClearHistoryEntryScenario1) {
  // Tests a common scenario: First, a user accidentally inputs an incomplete
  // romaji sequence and the predictor learns it.  Then, the user deletes it.
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Set up history. Convert "????????????" to "?????????r" 3 times.  This emulates a
  // case that a user accidentally input incomplete sequence.
  for (int i = 0; i < 3; ++i) {
    Segments segments;
    SetUpInputForConversion("????????????", composer_.get(), &segments);
    AddCandidate("?????????r", &segments);
    predictor->Finish(*convreq_, &segments);
  }

  // Test if the predictor learned "?????????r".
  EXPECT_TRUE(IsSuggested(predictor, "?????????", "?????????r"));
  EXPECT_TRUE(IsPredicted(predictor, "?????????", "?????????r"));

  // The user tris deleting the history ("????????????", "?????????r").
  EXPECT_TRUE(predictor->ClearHistoryEntry("????????????", "?????????r"));

  // The predictor shouldn't show "?????????r" both for suggestion and prediction.
  EXPECT_FALSE(IsSuggested(predictor, "?????????", "?????????r"));
  EXPECT_FALSE(IsPredicted(predictor, "?????????", "?????????r"));
}

TEST_F(UserHistoryPredictorTest, ClearHistoryEntryScenario2) {
  // Tests a common scenario: First, a user inputs a sentence ending with a
  // symbol and it's learned by the predictor.  Then, the user deletes the
  // history containing the symbol.
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Set up history. Convert "??????????????????????????????" to "?????????????????????!" 3 times
  // so that the predictor learns the sentence. We assume that this sentence
  // consists of three segments: "?????????|????????????|!".
  for (int i = 0; i < 3; ++i) {
    Segments segments;

    // The first segment: ("????????????", "?????????")
    Segment *seg = segments.add_segment();
    seg->set_key("????????????");
    seg->set_segment_type(Segment::FIXED_VALUE);
    Segment::Candidate *candidate = seg->add_candidate();
    candidate->Init();
    candidate->value = "?????????";
    candidate->content_value = "??????";
    candidate->key = seg->key();
    candidate->content_key = "?????????";

    // The second segment: ("???????????????", "????????????")
    seg = segments.add_segment();
    seg->set_key("???????????????");
    seg->set_segment_type(Segment::FIXED_VALUE);
    candidate = seg->add_candidate();
    candidate->Init();
    candidate->value = "????????????";
    candidate->content_value = candidate->value;
    candidate->key = seg->key();
    candidate->content_key = seg->key();

    // The third segment: ("???", "!")
    seg = segments.add_segment();
    seg->set_key("???");
    seg->set_segment_type(Segment::FIXED_VALUE);
    candidate = seg->add_candidate();
    candidate->Init();
    candidate->value = "!";
    candidate->content_value = "!";
    candidate->key = seg->key();
    candidate->content_key = seg->key();

    predictor->Finish(*convreq_, &segments);
  }

  // Check if the predictor learned the sentence.  Since the symbol is contained
  // in one segment, both "?????????????????????" and "?????????????????????!" should be
  // suggested and predicted.
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "????????????", "?????????????????????"));
  EXPECT_TRUE(
      IsSuggestedAndPredicted(predictor, "????????????", "?????????????????????!"));

  // Now the user deletes the sentence containing the "!".
  EXPECT_TRUE(
      predictor->ClearHistoryEntry("??????????????????????????????", "?????????????????????!"));

  // The sentence "?????????????????????" should still be suggested and predicted.
  EXPECT_TRUE(IsSuggestedAndPredicted(predictor, "????????????", "?????????????????????"));

  // However, "?????????????????????!" should be neither suggested nor predicted.
  EXPECT_FALSE(IsSuggested(predictor, "????????????", "?????????????????????!"));
  EXPECT_FALSE(IsPredicted(predictor, "????????????", "?????????????????????!"));
}

TEST_F(UserHistoryPredictorTest, ContentWordLearningFromInnerSegmentBoundary) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();
  predictor->set_content_word_learning_enabled(true);

  Segments segments;
  {
    constexpr char kKey[] = "??????????????????????????????????????????";
    constexpr char kValue[] = "?????????????????????????????????";
    SetUpInputForPrediction(kKey, composer_.get(), &segments);
    Segment::Candidate *candidate =
        segments.mutable_segment(0)->add_candidate();
    candidate->Init();
    candidate->key = kKey;
    candidate->value = kValue;
    candidate->content_key = kKey;
    candidate->content_value = kValue;
    candidate->PushBackInnerSegmentBoundary(18, 9, 15, 6);
    candidate->PushBackInnerSegmentBoundary(12, 12, 9, 9);
    candidate->PushBackInnerSegmentBoundary(12, 12, 12, 12);
    predictor->Finish(*convreq_, &segments);
  }

  segments.Clear();
  SetUpInputForPrediction("???", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_TRUE(FindCandidateByValue("??????", segments));
  EXPECT_TRUE(FindCandidateByValue("?????????", segments));

  segments.Clear();
  SetUpInputForPrediction("???", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_TRUE(FindCandidateByValue("?????????", segments));
  EXPECT_TRUE(FindCandidateByValue("????????????", segments));

  segments.Clear();
  SetUpInputForPrediction("???", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_TRUE(FindCandidateByValue("????????????", segments));
}

TEST_F(UserHistoryPredictorTest, JoinedSegmentsTestMobile) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();
  commands::RequestForUnitTest::FillMobileRequest(request_.get());
  Segments segments;

  SetUpInputForConversion("????????????", composer_.get(), &segments);
  AddCandidate(0, "??????", &segments);

  AddSegmentForConversion("????????????", &segments);
  AddCandidate(1, "?????????", &segments);

  predictor->Finish(*convreq_, &segments);
  segments.Clear();

  SetUpInputForSuggestion("?????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ(1, segments.segment(0).candidates_size());
  EXPECT_EQ("??????", segments.segment(0).candidate(0).value);
  EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);
  segments.Clear();

  SetUpInputForPrediction("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ(1, segments.segment(0).candidates_size());
  EXPECT_EQ("??????", segments.segment(0).candidate(0).value);
  EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);
  segments.Clear();

  SetUpInputForPrediction("???????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ(1, segments.segment(0).candidates_size());
  EXPECT_EQ("???????????????", segments.segment(0).candidate(0).value);
  EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);
  segments.Clear();
}

TEST_F(UserHistoryPredictorTest, JoinedSegmentsTestDesktop) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;

  SetUpInputForConversion("????????????", composer_.get(), &segments);
  AddCandidate(0, "??????", &segments);

  AddSegmentForConversion("????????????", &segments);
  AddCandidate(1, "?????????", &segments);

  predictor->Finish(*convreq_, &segments);

  segments.Clear();

  SetUpInputForSuggestion("?????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ(2, segments.segment(0).candidates_size());
  EXPECT_EQ("??????", segments.segment(0).candidate(0).value);
  EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);
  EXPECT_EQ("???????????????", segments.segment(0).candidate(1).value);
  EXPECT_TRUE(segments.segment(0).candidate(1).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);
  segments.Clear();

  SetUpInputForPrediction("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ(1, segments.segment(0).candidates_size());
  EXPECT_EQ("???????????????", segments.segment(0).candidate(0).value);
  EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);
  segments.Clear();

  SetUpInputForPrediction("???????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_EQ(1, segments.segment(0).candidates_size());
  EXPECT_EQ("???????????????", segments.segment(0).candidate(0).value);
  EXPECT_TRUE(segments.segment(0).candidate(0).source_info &
              Segment::Candidate::USER_HISTORY_PREDICTOR);
  segments.Clear();
}

TEST_F(UserHistoryPredictorTest, UsageStats) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  Segments segments;
  EXPECT_COUNT_STATS("CommitUserHistoryPredictor", 0);
  EXPECT_COUNT_STATS("CommitUserHistoryPredictorZeroQuery", 0);

  SetUpInputForConversion("????????????", composer_.get(), &segments);
  AddCandidate(0, "?????????", &segments);
  segments.mutable_conversion_segment(0)->mutable_candidate(0)->source_info |=
      Segment::Candidate::USER_HISTORY_PREDICTOR;
  predictor->Finish(*convreq_, &segments);

  EXPECT_COUNT_STATS("CommitUserHistoryPredictor", 1);
  EXPECT_COUNT_STATS("CommitUserHistoryPredictorZeroQuery", 0);

  segments.Clear();

  // Zero query
  SetUpInputForConversion("", composer_.get(), &segments);
  AddCandidate(0, "?????????", &segments);
  segments.mutable_conversion_segment(0)->mutable_candidate(0)->source_info |=
      Segment::Candidate::USER_HISTORY_PREDICTOR;
  predictor->Finish(*convreq_, &segments);

  // UserHistoryPredictor && ZeroQuery
  EXPECT_COUNT_STATS("CommitUserHistoryPredictor", 2);
  EXPECT_COUNT_STATS("CommitUserHistoryPredictorZeroQuery", 1);
}

TEST_F(UserHistoryPredictorTest, PunctuationLinkMobile) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();
  commands::RequestForUnitTest::FillMobileRequest(request_.get());
  Segments segments;
  {
    SetUpInputForConversion("???????????????", composer_.get(), &segments);
    AddCandidate(0, "???????????????", &segments);
    predictor->Finish(*convreq_, &segments);

    SetUpInputForConversionWithHistory("!", "???????????????", "???????????????",
                                       composer_.get(), &segments);
    AddCandidate(1, "???", &segments);
    predictor->Finish(*convreq_, &segments);

    segments.Clear();
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????", segments.conversion_segment(0).candidate(0).value);
    EXPECT_FALSE(FindCandidateByValue("??????????????????", segments));

    // Zero query from "???????????????" -> "???"
    segments.Clear();
    SetUpInputForConversion("???????????????", composer_.get(), &segments);
    AddCandidate(0, "???????????????", &segments);
    SetUpInputForSuggestionWithHistory("", "???????????????", "???????????????",
                                       composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???", segments.conversion_segment(0).candidate(0).value);
  }

  predictor->ClearAllHistory();
  WaitForSyncer(predictor);

  {
    SetUpInputForConversion("!", composer_.get(), &segments);
    AddCandidate(0, "???", &segments);
    predictor->Finish(*convreq_, &segments);

    SetUpInputForSuggestionWithHistory("???????????????", "!", "???", composer_.get(),
                                       &segments);
    AddCandidate(1, "???????????????", &segments);
    predictor->Finish(*convreq_, &segments);

    // Zero query from "???" -> no suggestion
    segments.Clear();
    SetUpInputForSuggestionWithHistory("", "!", "???", composer_.get(),
                                       &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
  }

  predictor->ClearAllHistory();
  WaitForSyncer(predictor);

  {
    SetUpInputForConversion("???????????????!", composer_.get(), &segments);
    AddCandidate(0, "??????????????????", &segments);

    predictor->Finish(*convreq_, &segments);
    segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

    AddSegmentForConversion("?????????????????????????????????", &segments);
    AddCandidate(1, "??????????????????????????????", &segments);
    predictor->Finish(*convreq_, &segments);

    // Zero query from "???" -> no suggestion
    segments.Clear();
    SetUpInputForConversion("!", composer_.get(), &segments);
    AddCandidate(0, "???", &segments);
    segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);
    AddSegmentForSuggestion("", &segments);  // empty request
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));

    // Zero query from "??????????????????" -> no suggestion
    segments.Clear();
    SetUpInputForConversion("???????????????!", composer_.get(), &segments);
    AddCandidate(0, "??????????????????", &segments);
    segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);
    AddSegmentForSuggestion("", &segments);  // empty request
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
  }

  predictor->ClearAllHistory();
  WaitForSyncer(predictor);

  {
    SetUpInputForConversion("???????????????", composer_.get(), &segments);
    AddCandidate(0, "???????????????", &segments);
    predictor->Finish(*convreq_, &segments);

    SetUpInputForConversionWithHistory("!?????????????????????????????????", "???????????????",
                                       "???????????????", composer_.get(),
                                       &segments);
    AddCandidate(1, "?????????????????????????????????", &segments);
    predictor->Finish(*convreq_, &segments);

    segments.Clear();
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????", segments.conversion_segment(0).candidate(0).value);
    EXPECT_FALSE(
        FindCandidateByValue("????????????????????????????????????????????????", segments));

    // Zero query from "???????????????" -> no suggestion
    SetUpInputForConversionWithHistory("", "???????????????", "???????????????",
                                       composer_.get(), &segments);
    AddSegmentForSuggestion("", &segments);  // empty request
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
  }
}

TEST_F(UserHistoryPredictorTest, PunctuationLinkDesktop) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();
  Segments segments;
  {
    SetUpInputForConversion("???????????????", composer_.get(), &segments);
    AddCandidate(0, "???????????????", &segments);

    predictor->Finish(*convreq_, &segments);
    segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

    AddSegmentForConversion("!", &segments);
    AddCandidate(1, "???", &segments);
    predictor->Finish(*convreq_, &segments);

    segments.Clear();
    SetUpInputForSuggestion("????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????", segments.conversion_segment(0).candidate(0).value);
    EXPECT_FALSE(FindCandidateByValue("??????????????????", segments));

    segments.Clear();
    SetUpInputForSuggestion("???????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????", segments.conversion_segment(0).candidate(0).value);
    EXPECT_FALSE(FindCandidateByValue("??????????????????", segments));
  }

  predictor->ClearAllHistory();
  WaitForSyncer(predictor);

  {
    SetUpInputForConversion("!", composer_.get(), &segments);
    AddCandidate(0, "???", &segments);

    predictor->Finish(*convreq_, &segments);
    segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

    AddSegmentForConversion("?????????????????????????????????", &segments);
    AddCandidate(1, "??????????????????????????????", &segments);
    predictor->Finish(*convreq_, &segments);

    segments.Clear();
    SetUpInputForSuggestion("!", composer_.get(), &segments);
    EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
  }

  predictor->ClearAllHistory();
  WaitForSyncer(predictor);

  {
    SetUpInputForConversion("???????????????!", composer_.get(), &segments);
    AddCandidate(0, "??????????????????", &segments);

    predictor->Finish(*convreq_, &segments);
    segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

    AddSegmentForConversion("?????????????????????????????????", &segments);
    AddCandidate(1, "??????????????????????????????", &segments);
    predictor->Finish(*convreq_, &segments);

    segments.Clear();
    SetUpInputForSuggestion("???????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("??????????????????",
              segments.conversion_segment(0).candidate(0).value);
    EXPECT_FALSE(
        FindCandidateByValue("????????????????????????????????????????????????", segments));

    segments.Clear();
    SetUpInputForSuggestion("???????????????!", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("??????????????????",
              segments.conversion_segment(0).candidate(0).value);
    EXPECT_FALSE(
        FindCandidateByValue("????????????????????????????????????????????????", segments));
  }

  predictor->ClearAllHistory();
  WaitForSyncer(predictor);

  {
    SetUpInputForConversion("???????????????", composer_.get(), &segments);
    AddCandidate(0, "???????????????", &segments);

    predictor->Finish(*convreq_, &segments);
    segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

    AddSegmentForConversion("!?????????????????????????????????", &segments);
    AddCandidate(1, "?????????????????????????????????", &segments);
    predictor->Finish(*convreq_, &segments);

    segments.Clear();
    SetUpInputForSuggestion("???????????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ("???????????????", segments.conversion_segment(0).candidate(0).value);
    EXPECT_FALSE(FindCandidateByValue("??????????????????", segments));
    EXPECT_FALSE(
        FindCandidateByValue("????????????????????????????????????????????????", segments));
  }

  predictor->ClearAllHistory();
  WaitForSyncer(predictor);

  {
    // Note that "??????????????????????????????:?????????????????????????????????" is the sentence
    // like candidate. Please refer to user_history_predictor.cc
    SetUpInputForConversion("?????????????????????????????????", composer_.get(),
                            &segments);
    AddCandidate(0, "??????????????????????????????", &segments);

    predictor->Finish(*convreq_, &segments);
    segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);

    AddSegmentForConversion("!", &segments);
    AddCandidate(1, "???", &segments);
    predictor->Finish(*convreq_, &segments);

    segments.Clear();
    SetUpInputForSuggestion("?????????????????????????????????", composer_.get(),
                            &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_TRUE(FindCandidateByValue("?????????????????????????????????", segments));
  }
}

TEST_F(UserHistoryPredictorTest, 62DayOldEntriesAreDeletedAtSync) {
  ScopedClockMock clock(1, 0);

  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Let the predictor learn "???????????????????????????".
  Segments segments;
  SetUpInputForConversion("???????????????????????????????????????", composer_.get(),
                          &segments);
  AddCandidate("???????????????????????????", &segments);
  predictor->Finish(*convreq_, &segments);

  // Verify that "???????????????????????????" is predicted.
  segments.Clear();
  SetUpInputForPrediction("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_TRUE(FindCandidateByValue("???????????????????????????", segments));

  // Now, simulate the case where 63 days passed.
  clock->PutClockForward(63 * 24 * 60 * 60, 0);

  // Let the predictor learn "???????????????????????????".
  segments.Clear();
  SetUpInputForConversion("??????????????????????????????????????????", composer_.get(),
                          &segments);
  AddCandidate("???????????????????????????", &segments);
  predictor->Finish(*convreq_, &segments);

  // Verify that "???????????????????????????" is predicted but "???????????????????????????" is
  // not.  The latter one is still in on-memory data structure but lookup is
  // prevented.  The entry is removed when the data is written to disk.
  segments.Clear();
  SetUpInputForPrediction("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_TRUE(FindCandidateByValue("???????????????????????????", segments));
  EXPECT_FALSE(FindCandidateByValue("???????????????????????????", segments));

  // Here, write the history to a storage.
  ASSERT_TRUE(predictor->Sync());
  WaitForSyncer(predictor);

  // Verify that "???????????????????????????" is no longer predicted because it was
  // learned 63 days before.
  segments.Clear();
  SetUpInputForPrediction("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_TRUE(FindCandidateByValue("???????????????????????????", segments));
  EXPECT_FALSE(FindCandidateByValue("???????????????????????????", segments));

  // Verify also that on-memory data structure doesn't contain node for ??????.
  bool found_takahashi = false;
  for (const auto *elem = predictor->dic_->Head(); elem != nullptr;
       elem = elem->next) {
    EXPECT_EQ(std::string::npos, elem->value.value().find("??????"));
    if (elem->value.value().find("??????")) {
      found_takahashi = true;
    }
  }
  EXPECT_TRUE(found_takahashi);
}

TEST_F(UserHistoryPredictorTest, FutureTimestamp) {
  // Test the case where history has "future" timestamps.
  ScopedClockMock clock(10000, 0);

  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();

  // Let the predictor learn "???????????????????????????".
  Segments segments;
  SetUpInputForConversion("???????????????????????????????????????", composer_.get(),
                          &segments);
  AddCandidate("???????????????????????????", &segments);
  predictor->Finish(*convreq_, &segments);

  // Verify that "???????????????????????????" is predicted.
  segments.Clear();
  SetUpInputForPrediction("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_TRUE(FindCandidateByValue("???????????????????????????", segments));

  // Now, go back to the past.
  clock->SetTime(1, 0);

  // Verify that "???????????????????????????" is predicted without crash.
  segments.Clear();
  SetUpInputForPrediction("????????????", composer_.get(), &segments);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_TRUE(FindCandidateByValue("???????????????????????????", segments));
}

TEST_F(UserHistoryPredictorTest, MaxPredictionCandidatesSize) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();
  Segments segments;
  {
    SetUpInputForPrediction("?????????", composer_.get(), &segments);
    AddCandidate(0, "?????????", &segments);
    predictor->Finish(*convreq_, &segments);
  }
  {
    SetUpInputForPrediction("?????????", composer_.get(), &segments);
    AddCandidate(0, "?????????", &segments);
    predictor->Finish(*convreq_, &segments);
  }
  {
    SetUpInputForPrediction("?????????", composer_.get(), &segments);
    AddCandidate(0, "Test", &segments);
    predictor->Finish(*convreq_, &segments);
  }
  {
    convreq_->set_max_user_history_prediction_candidates_size(2);
    MakeSegmentsForSuggestion("?????????", &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ(1, segments.segments_size());
    EXPECT_EQ(2, segments.segment(0).candidates_size());

    MakeSegmentsForPrediction("?????????", &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ(1, segments.segments_size());
    EXPECT_EQ(2, segments.segment(0).candidates_size());
  }
  {
    convreq_->set_max_user_history_prediction_candidates_size(3);
    SetUpInputForSuggestion("?????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ(1, segments.segments_size());
    EXPECT_EQ(3, segments.segment(0).candidates_size());

    SetUpInputForPrediction("?????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ(1, segments.segments_size());
    EXPECT_EQ(3, segments.segment(0).candidates_size());
  }

  {
    // Only 3 candidates in user history
    convreq_->set_max_user_history_prediction_candidates_size(4);
    SetUpInputForSuggestion("?????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ(1, segments.segments_size());
    EXPECT_EQ(3, segments.segment(0).candidates_size());

    SetUpInputForPrediction("?????????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ(1, segments.segments_size());
    EXPECT_EQ(3, segments.segment(0).candidates_size());
  }
}

TEST_F(UserHistoryPredictorTest, MaxPredictionCandidatesSizeForZeroQuery) {
  UserHistoryPredictor *predictor = GetUserHistoryPredictorWithClearedHistory();
  commands::RequestForUnitTest::FillMobileRequest(request_.get());
  Segments segments;
  {
    SetUpInputForPrediction("?????????", composer_.get(), &segments);
    AddCandidate(0, "?????????", &segments);
    predictor->Finish(*convreq_, &segments);
    segments.mutable_segment(0)->set_segment_type(Segment::HISTORY);
  }
  {
    AddSegmentForPrediction("??????", &segments);
    AddCandidate(1, "????", &segments);
    predictor->Finish(*convreq_, &segments);
  }
  {
    Segment::Candidate *candidate =
        segments.mutable_segment(1)->mutable_candidate(0);
    candidate->value = "????";
    candidate->content_value = candidate->value;
    predictor->Finish(*convreq_, &segments);
  }
  {
    Segment::Candidate *candidate =
        segments.mutable_segment(1)->mutable_candidate(0);
    candidate->value = "????";
    candidate->content_value = candidate->value;
    predictor->Finish(*convreq_, &segments);
  }

  convreq_->set_max_user_history_prediction_candidates_size(2);
  convreq_->set_max_user_history_prediction_candidates_size_for_zero_query(3);
  // normal prediction candidates size
  {
    SetUpInputForSuggestion("??????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ(1, segments.segments_size());
    EXPECT_EQ(2, segments.segment(0).candidates_size());

    SetUpInputForPrediction("??????", composer_.get(), &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ(1, segments.segments_size());
    EXPECT_EQ(2, segments.segment(0).candidates_size());
  }

  // prediction candidates for zero query
  {
    SetUpInputForSuggestionWithHistory("", "?????????", "?????????", composer_.get(),
                                       &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ(1, segments.conversion_segments_size());
    EXPECT_EQ(3, segments.conversion_segment(0).candidates_size());

    SetUpInputForPredictionWithHistory("", "?????????", "?????????", composer_.get(),
                                       &segments);
    EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
    EXPECT_EQ(1, segments.conversion_segments_size());
    EXPECT_EQ(3, segments.conversion_segment(0).candidates_size());
  }
}

}  // namespace mozc
