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

#include "rewriter/number_rewriter.h"

#include <cstddef>
#include <iterator>
#include <memory>
#include <string>

#include "base/logging.h"
#include "base/port.h"
#include "base/util.h"
#include "config/config_handler.h"
#include "converter/segments.h"
#include "data_manager/testing/mock_data_manager.h"
#include "dictionary/pos_matcher.h"
#include "protocol/commands.pb.h"
#include "request/conversion_request.h"
#include "testing/base/public/gunit.h"
#include "testing/base/public/mozctest.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"

// To show the value of size_t, 'z' speficier should be used.
// But MSVC doesn't support it yet so use 'l' instead.
#ifdef _MSC_VER
#define SIZE_T_PRINTF_FORMAT "%lu"
#else  // _MSC_VER
#define SIZE_T_PRINTF_FORMAT "%zu"
#endif  // _MSC_VER

namespace mozc {
namespace {

using dictionary::PosMatcher;

constexpr char kKanjiDescription[] = "?????????";
constexpr char kArabicDescription[] = "??????";
constexpr char kOldKanjiDescription[] = "??????";
constexpr char kMaruNumberDescription[] = "?????????";
constexpr char kRomanCapitalDescription[] = "???????????????(?????????)";
constexpr char kRomanNoCapitalDescription[] = "???????????????(?????????)";

bool FindValue(const Segment &segment, const std::string &value) {
  for (size_t i = 0; i < segment.candidates_size(); ++i) {
    if (segment.candidate(i).value == value) {
      return true;
    }
  }
  return false;
}

Segment *SetupSegments(const PosMatcher &pos_matcher,
                       const std::string &candidate_value, Segments *segments) {
  segments->Clear();
  Segment *segment = segments->push_back_segment();
  Segment::Candidate *candidate = segment->add_candidate();
  candidate->Init();
  candidate->lid = pos_matcher.GetNumberId();
  candidate->rid = pos_matcher.GetNumberId();
  candidate->value = candidate_value;
  candidate->content_value = candidate_value;
  return segment;
}

bool HasDescription(const Segment &segment, const std::string &description) {
  for (size_t i = 0; i < segment.candidates_size(); ++i) {
    if (segment.candidate(i).description == description) {
      return true;
    }
  }
  return false;
}

// Find candidate id
bool FindCandidateId(const Segment &segment, const std::string &value,
                     int *id) {
  for (size_t i = 0; i < segment.candidates_size(); ++i) {
    if (segment.candidate(i).value == value) {
      *id = i;
      return true;
    }
  }
  return false;
}
}  // namespace

class NumberRewriterTest : public ::testing::Test {
 protected:
  // Explicitly define constructor to prevent Visual C++ from
  // considering this class as POD.
  NumberRewriterTest() {}

  void SetUp() override {
    pos_matcher_.Set(mock_data_manager_.GetPosMatcherData());
  }

  NumberRewriter *CreateNumberRewriter() {
    return new NumberRewriter(&mock_data_manager_);
  }

  const testing::MockDataManager mock_data_manager_;
  PosMatcher pos_matcher_;
  const ConversionRequest default_request_;

 private:
  testing::ScopedTmpUserProfileDirectory tmp_profile_dir_;
};

namespace {
struct ExpectResult {
  const char *value;
  const char *content_value;
  const char *description;
};
}  // namespace

TEST_F(NumberRewriterTest, BasicTest) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  Segment *seg = segments.push_back_segment();
  Segment::Candidate *candidate = seg->add_candidate();
  candidate->Init();
  candidate->lid = pos_matcher_.GetNumberId();
  candidate->rid = pos_matcher_.GetNumberId();
  candidate->value = "012";
  candidate->content_value = "012";
  candidate->key = "012";
  candidate->content_key = "012";

  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));

  const ExpectResult kExpectResults[] = {
      {"012", "012", ""},
      {"?????????", "?????????", kKanjiDescription},
      {"?????????", "?????????", kArabicDescription},
      {"??????", "??????", kKanjiDescription},
      {"?????????", "?????????", kOldKanjiDescription},
      {"???", "???", kRomanCapitalDescription},
      {"???", "???", kRomanNoCapitalDescription},
      {"???", "???", kMaruNumberDescription},
      {"0xc", "0xc", "16??????"},
      {"014", "014", "8??????"},
      {"0b1100", "0b1100", "2??????"},
  };

  constexpr size_t kExpectResultSize = std::size(kExpectResults);
  EXPECT_EQ(kExpectResultSize, seg->candidates_size());

  for (size_t i = 0; i < kExpectResultSize; ++i) {
    SCOPED_TRACE(absl::StrFormat("i = " SIZE_T_PRINTF_FORMAT, i));
    EXPECT_EQ(kExpectResults[i].value, seg->candidate(i).value);
    EXPECT_EQ(kExpectResults[i].content_value, seg->candidate(i).content_value);
    EXPECT_EQ(kExpectResults[i].description, seg->candidate(i).description);
  }
  seg->clear_candidates();
}

TEST_F(NumberRewriterTest, RequestType) {
  class TestData {
   public:
    ConversionRequest::RequestType request_type_;
    int expected_candidate_number_;
    TestData(ConversionRequest::RequestType request_type, int expected_number)
        : request_type_(request_type),
          expected_candidate_number_(expected_number) {}
  };
  TestData test_data_list[] = {
      TestData(ConversionRequest::CONVERSION, 11),  // 11 comes from BasicTest
      TestData(ConversionRequest::REVERSE_CONVERSION, 8),
      TestData(ConversionRequest::PREDICTION, 8),
      TestData(ConversionRequest::SUGGESTION, 8),
  };

  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  for (size_t i = 0; i < std::size(test_data_list); ++i) {
    TestData &test_data = test_data_list[i];
    Segments segments;
    Segment *seg = segments.push_back_segment();
    Segment::Candidate *candidate = seg->add_candidate();
    candidate->Init();
    candidate->lid = pos_matcher_.GetNumberId();
    candidate->rid = pos_matcher_.GetNumberId();
    candidate->value = "012";
    candidate->content_value = "012";
    ConversionRequest request;
    request.set_request_type(test_data.request_type_);
    EXPECT_TRUE(number_rewriter->Rewrite(request, &segments));
    EXPECT_EQ(test_data.expected_candidate_number_, seg->candidates_size());
  }
}

TEST_F(NumberRewriterTest, BasicTestWithSuffix) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  Segment *seg = segments.push_back_segment();
  Segment::Candidate *candidate = seg->add_candidate();
  candidate->Init();
  candidate->lid = pos_matcher_.GetNumberId();
  candidate->rid = pos_matcher_.GetNumberId();
  candidate->value = "012???";
  candidate->content_value = "012";
  candidate->key = "012???";
  candidate->content_key = "012";

  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));

  const ExpectResult kExpectResults[] = {
      {"012???", "012", ""},
      {"????????????", "?????????", kKanjiDescription},
      {"????????????", "?????????", kArabicDescription},
      {"?????????", "??????", kKanjiDescription},
      {"????????????", "?????????", kOldKanjiDescription},
      {"??????", "???", kRomanCapitalDescription},
      {"??????", "???", kRomanNoCapitalDescription},
      {"??????", "???", kMaruNumberDescription},
      {"0xc???", "0xc", "16??????"},
      {"014???", "014", "8??????"},
      {"0b1100???", "0b1100", "2??????"},
  };

  constexpr size_t kExpectResultSize = std::size(kExpectResults);
  EXPECT_EQ(kExpectResultSize, seg->candidates_size());

  for (size_t i = 0; i < kExpectResultSize; ++i) {
    SCOPED_TRACE(absl::StrFormat("i = " SIZE_T_PRINTF_FORMAT, i));
    EXPECT_EQ(kExpectResults[i].value, seg->candidate(i).value);
    EXPECT_EQ(kExpectResults[i].content_value, seg->candidate(i).content_value);
    EXPECT_EQ(kExpectResults[i].description, seg->candidate(i).description);
  }

  seg->clear_candidates();
}

TEST_F(NumberRewriterTest, BasicTestWithNumberSuffix) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  Segment *seg = segments.push_back_segment();
  Segment::Candidate *candidate = seg->add_candidate();
  candidate->Init();
  candidate->lid = pos_matcher_.GetNumberId();
  candidate->rid = pos_matcher_.GetCounterSuffixWordId();
  candidate->value = "?????????";
  candidate->content_value = "?????????";

  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));

  EXPECT_EQ(2, seg->candidates_size());

  EXPECT_EQ("?????????", seg->candidate(0).value);
  EXPECT_EQ("?????????", seg->candidate(0).content_value);
  EXPECT_EQ("", seg->candidate(0).description);

  EXPECT_EQ("15???", seg->candidate(1).value);
  EXPECT_EQ("15???", seg->candidate(1).content_value);
  EXPECT_EQ("", seg->candidate(1).description);
  seg->clear_candidates();
}

TEST_F(NumberRewriterTest, TestWithMultipleNumberSuffix) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  Segment *seg = segments.push_back_segment();
  Segment::Candidate *candidate = seg->add_candidate();
  candidate->Init();
  candidate->lid = pos_matcher_.GetNumberId();
  candidate->rid = pos_matcher_.GetCounterSuffixWordId();
  candidate->value = "?????????";
  candidate->content_value = "?????????";
  candidate = seg->add_candidate();
  candidate->Init();
  candidate->lid = pos_matcher_.GetNumberId();
  candidate->rid = pos_matcher_.GetCounterSuffixWordId();
  candidate->value = "?????????";
  candidate->content_value = "?????????";

  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));

  EXPECT_EQ(4, seg->candidates_size());

  EXPECT_EQ("?????????", seg->candidate(0).value);
  EXPECT_EQ("?????????", seg->candidate(0).content_value);
  EXPECT_EQ("", seg->candidate(0).description);

  EXPECT_EQ("15???", seg->candidate(1).value);
  EXPECT_EQ("15???", seg->candidate(1).content_value);
  EXPECT_EQ("", seg->candidate(1).description);

  EXPECT_EQ("?????????", seg->candidate(2).value);
  EXPECT_EQ("?????????", seg->candidate(2).content_value);
  EXPECT_EQ("", seg->candidate(2).description);

  EXPECT_EQ("15???", seg->candidate(3).value);
  EXPECT_EQ("15???", seg->candidate(3).content_value);
  EXPECT_EQ("", seg->candidate(3).description);

  seg->clear_candidates();
}

TEST_F(NumberRewriterTest, SpecialFormBoundaries) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());
  Segments segments;

  // Special forms doesn't have zeros.
  Segment *seg = SetupSegments(pos_matcher_, "0", &segments);
  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));
  EXPECT_FALSE(HasDescription(*seg, kMaruNumberDescription));
  EXPECT_FALSE(HasDescription(*seg, kRomanCapitalDescription));
  EXPECT_FALSE(HasDescription(*seg, kRomanNoCapitalDescription));

  // "1" has special forms.
  seg = SetupSegments(pos_matcher_, "1", &segments);
  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));
  EXPECT_TRUE(HasDescription(*seg, kMaruNumberDescription));
  EXPECT_TRUE(HasDescription(*seg, kRomanCapitalDescription));
  EXPECT_TRUE(HasDescription(*seg, kRomanNoCapitalDescription));

  // "12" has every special forms.
  seg = SetupSegments(pos_matcher_, "12", &segments);
  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));
  EXPECT_TRUE(HasDescription(*seg, kMaruNumberDescription));
  EXPECT_TRUE(HasDescription(*seg, kRomanCapitalDescription));
  EXPECT_TRUE(HasDescription(*seg, kRomanNoCapitalDescription));

  // "13" doesn't have roman forms.
  seg = SetupSegments(pos_matcher_, "13", &segments);
  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));
  EXPECT_TRUE(HasDescription(*seg, kMaruNumberDescription));
  EXPECT_FALSE(HasDescription(*seg, kRomanCapitalDescription));
  EXPECT_FALSE(HasDescription(*seg, kRomanNoCapitalDescription));

  // "50" has circled numerics.
  seg = SetupSegments(pos_matcher_, "50", &segments);
  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));
  EXPECT_TRUE(HasDescription(*seg, kMaruNumberDescription));
  EXPECT_FALSE(HasDescription(*seg, kRomanCapitalDescription));
  EXPECT_FALSE(HasDescription(*seg, kRomanNoCapitalDescription));

  // "51" doesn't have special forms.
  seg = SetupSegments(pos_matcher_, "51", &segments);
  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));
  EXPECT_FALSE(HasDescription(*seg, kMaruNumberDescription));
  EXPECT_FALSE(HasDescription(*seg, kRomanCapitalDescription));
  EXPECT_FALSE(HasDescription(*seg, kRomanNoCapitalDescription));
}

TEST_F(NumberRewriterTest, OneOfCandidatesIsEmpty) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  Segment *seg = segments.push_back_segment();
  Segment::Candidate *first_candidate = seg->add_candidate();
  first_candidate->Init();

  // this candidate should be skipped
  first_candidate->value = "";
  first_candidate->content_value = first_candidate->value;

  Segment::Candidate *second_candidate = seg->add_candidate();
  second_candidate->Init();

  second_candidate->value = "0";
  second_candidate->lid = pos_matcher_.GetNumberId();
  second_candidate->rid = pos_matcher_.GetNumberId();
  second_candidate->content_value = second_candidate->value;
  second_candidate->key = "0";
  second_candidate->content_key = "0";

  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));

  EXPECT_EQ("", seg->candidate(0).value);
  EXPECT_EQ("", seg->candidate(0).content_value);
  EXPECT_EQ("", seg->candidate(0).description);

  EXPECT_EQ("0", seg->candidate(1).value);
  EXPECT_EQ("0", seg->candidate(1).content_value);
  EXPECT_EQ("", seg->candidate(1).description);

  EXPECT_EQ("???", seg->candidate(2).value);
  EXPECT_EQ("???", seg->candidate(2).content_value);
  EXPECT_EQ(kKanjiDescription, seg->candidate(2).description);

  EXPECT_EQ("???", seg->candidate(3).value);
  EXPECT_EQ("???", seg->candidate(3).content_value);
  EXPECT_EQ(kArabicDescription, seg->candidate(3).description);

  EXPECT_EQ("???", seg->candidate(4).value);
  EXPECT_EQ("???", seg->candidate(4).content_value);
  EXPECT_EQ(kOldKanjiDescription, seg->candidate(4).description);

  seg->clear_candidates();
}

TEST_F(NumberRewriterTest, RewriteDoesNotHappen) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  Segment *seg = segments.push_back_segment();
  Segment::Candidate *candidate = seg->add_candidate();
  candidate->Init();

  candidate->value = "????????????";
  candidate->content_value = candidate->value;

  // Number rewrite should not occur
  EXPECT_FALSE(number_rewriter->Rewrite(default_request_, &segments));

  // Number of cahdidates should be maintained
  EXPECT_EQ(1, seg->candidates_size());

  seg->clear_candidates();
}

TEST_F(NumberRewriterTest, NumberIsZero) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  Segment *seg = segments.push_back_segment();
  Segment::Candidate *candidate = seg->add_candidate();
  candidate->Init();
  candidate->lid = pos_matcher_.GetNumberId();
  candidate->rid = pos_matcher_.GetNumberId();
  candidate->value = "0";
  candidate->content_value = "0";
  candidate->key = "0";
  candidate->content_key = "0";

  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));

  EXPECT_EQ(4, seg->candidates_size());

  EXPECT_EQ("0", seg->candidate(0).value);
  EXPECT_EQ("0", seg->candidate(0).content_value);
  EXPECT_EQ("", seg->candidate(0).description);

  EXPECT_EQ("???", seg->candidate(1).value);
  EXPECT_EQ("???", seg->candidate(1).content_value);
  EXPECT_EQ(kKanjiDescription, seg->candidate(1).description);

  EXPECT_EQ("???", seg->candidate(2).value);
  EXPECT_EQ("???", seg->candidate(2).content_value);
  EXPECT_EQ(kArabicDescription, seg->candidate(2).description);

  EXPECT_EQ("???", seg->candidate(3).value);
  EXPECT_EQ("???", seg->candidate(3).content_value);
  EXPECT_EQ(kOldKanjiDescription, seg->candidate(3).description);

  seg->clear_candidates();
}

TEST_F(NumberRewriterTest, NumberIsZeroZero) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  Segment *seg = segments.push_back_segment();
  Segment::Candidate *candidate = seg->add_candidate();
  candidate->Init();
  candidate->lid = pos_matcher_.GetNumberId();
  candidate->rid = pos_matcher_.GetNumberId();
  candidate->value = "00";
  candidate->content_value = "00";
  candidate->key = "00";
  candidate->content_key = "00";

  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));

  EXPECT_EQ(4, seg->candidates_size());

  EXPECT_EQ("00", seg->candidate(0).value);
  EXPECT_EQ("00", seg->candidate(0).content_value);
  EXPECT_EQ("", seg->candidate(0).description);

  EXPECT_EQ("??????", seg->candidate(1).value);
  EXPECT_EQ("??????", seg->candidate(1).content_value);
  EXPECT_EQ(kKanjiDescription, seg->candidate(1).description);

  EXPECT_EQ("??????", seg->candidate(2).value);
  EXPECT_EQ("??????", seg->candidate(2).content_value);
  EXPECT_EQ(kArabicDescription, seg->candidate(2).description);

  EXPECT_EQ("???", seg->candidate(3).value);
  EXPECT_EQ("???", seg->candidate(3).content_value);
  EXPECT_EQ(kOldKanjiDescription, seg->candidate(3).description);

  seg->clear_candidates();
}

TEST_F(NumberRewriterTest, NumberIs19Digit) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  Segment *seg = segments.push_back_segment();
  Segment::Candidate *candidate = seg->add_candidate();
  candidate->Init();
  candidate->lid = pos_matcher_.GetNumberId();
  candidate->rid = pos_matcher_.GetNumberId();
  candidate->value = "1000000000000000000";
  candidate->content_value = "1000000000000000000";
  candidate->key = "1000000000000000000";
  candidate->content_key = "1000000000000000000";

  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));

  const ExpectResult kExpectResults[] = {
      {"1000000000000000000", "1000000000000000000", ""},
      {"?????????????????????????????????????????????????????????",
       "?????????????????????????????????????????????????????????", kKanjiDescription},
      {"?????????????????????????????????????????????????????????",
       "?????????????????????????????????????????????????????????", kArabicDescription},
      {"1,000,000,000,000,000,000", "1,000,000,000,000,000,000",
       kArabicDescription},
      {"???????????????????????????????????????????????????????????????????????????",
       "???????????????????????????????????????????????????????????????????????????",
       kArabicDescription},
      {"100???", "100???", kArabicDescription},
      {"????????????", "????????????", kArabicDescription},
      {"??????", "??????", kKanjiDescription},
      {"?????????", "?????????", kOldKanjiDescription},
      {"0xde0b6b3a7640000", "0xde0b6b3a7640000", "16??????"},
      {"067405553164731000000", "067405553164731000000", "8??????"},
      {"0b110111100000101101101011001110100111011001000000000000000000",
       "0b110111100000101101101011001110100111011001000000000000000000",
       "2??????"},
  };

  constexpr size_t kExpectResultSize = std::size(kExpectResults);
  EXPECT_EQ(kExpectResultSize, seg->candidates_size());

  for (size_t i = 0; i < kExpectResultSize; ++i) {
    SCOPED_TRACE(absl::StrFormat("i = " SIZE_T_PRINTF_FORMAT, i));
    EXPECT_EQ(kExpectResults[i].value, seg->candidate(i).value);
    EXPECT_EQ(kExpectResults[i].content_value, seg->candidate(i).content_value);
    EXPECT_EQ(kExpectResults[i].description, seg->candidate(i).description);
  }

  seg->clear_candidates();
}

TEST_F(NumberRewriterTest, NumberIsGreaterThanUInt64Max) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  Segment *seg = segments.push_back_segment();
  Segment::Candidate *candidate = seg->add_candidate();
  candidate->Init();
  candidate->lid = pos_matcher_.GetNumberId();
  candidate->rid = pos_matcher_.GetNumberId();
  candidate->value = "18446744073709551616";  // 2^64
  candidate->content_value = "18446744073709551616";
  candidate->key = "18446744073709551616";
  candidate->content_key = "18446744073709551616";

  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));

  const ExpectResult kExpectResults[] = {
      {"18446744073709551616", "18446744073709551616", ""},
      {"????????????????????????????????????????????????????????????",
       "????????????????????????????????????????????????????????????", kKanjiDescription},
      {"????????????????????????????????????????????????????????????",
       "????????????????????????????????????????????????????????????", kArabicDescription},
      {"18,446,744,073,709,551,616", "18,446,744,073,709,551,616",
       kArabicDescription},
      {"??????????????????????????????????????????????????????????????????????????????",
       "??????????????????????????????????????????????????????????????????????????????",
       kArabicDescription},
      {"1844???6744???737???955???1616", "1844???6744???737???955???1616",
       kArabicDescription},
      {"??????????????????????????????????????????????????????????????????",
       "??????????????????????????????????????????????????????????????????", kArabicDescription},
      {"????????????????????????????????????????????????????????????????????????????????????????????????",
       "????????????????????????????????????????????????????????????????????????????????????????????????",
       kKanjiDescription},
      {"?????????????????????????????????????????????????????????????????????????????????????????????????????????",
       "?????????????????????????????????????????????????????????????????????????????????????????????????????????",
       kOldKanjiDescription},
  };

  constexpr size_t kExpectResultSize = std::size(kExpectResults);
  EXPECT_EQ(kExpectResultSize, seg->candidates_size());

  for (size_t i = 0; i < kExpectResultSize; ++i) {
    SCOPED_TRACE(absl::StrFormat("i = " SIZE_T_PRINTF_FORMAT, i));
    EXPECT_EQ(kExpectResults[i].value, seg->candidate(i).value);
    EXPECT_EQ(kExpectResults[i].content_value, seg->candidate(i).content_value);
    EXPECT_EQ(kExpectResults[i].description, seg->candidate(i).description);
  }

  seg->clear_candidates();
}

TEST_F(NumberRewriterTest, NumberIsGoogol) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  Segment *seg = segments.push_back_segment();
  Segment::Candidate *candidate = seg->add_candidate();
  candidate->Init();
  candidate->lid = pos_matcher_.GetNumberId();
  candidate->rid = pos_matcher_.GetNumberId();

  // 10^100 as "100000 ... 0"
  std::string input = "1";
  for (size_t i = 0; i < 100; ++i) {
    input += "0";
  }

  candidate->value = input;
  candidate->content_value = input;

  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));

  EXPECT_EQ(6, seg->candidates_size());

  EXPECT_EQ(input, seg->candidate(0).value);
  EXPECT_EQ(input, seg->candidate(0).content_value);
  EXPECT_EQ("", seg->candidate(0).description);

  // 10^100 as "?????????????????? ... ???"
  std::string expected2 = "???";
  for (size_t i = 0; i < 100; ++i) {
    expected2 += "???";
  }
  EXPECT_EQ(expected2, seg->candidate(1).value);
  EXPECT_EQ(expected2, seg->candidate(1).content_value);
  EXPECT_EQ(kKanjiDescription, seg->candidate(1).description);

  // 10^100 as "?????????????????? ... ???"
  std::string expected3 = "???";
  for (size_t i = 0; i < 100; ++i) {
    expected3 += "???";
  }
  EXPECT_EQ(expected3, seg->candidate(2).value);
  EXPECT_EQ(expected3, seg->candidate(2).content_value);
  EXPECT_EQ(kArabicDescription, seg->candidate(2).description);

  // 10,000, ... ,000
  std::string expected1 = "10";
  for (size_t i = 0; i < 100 / 3; ++i) {
    expected1 += ",000";
  }
  EXPECT_EQ(expected1, seg->candidate(3).value);
  EXPECT_EQ(expected1, seg->candidate(3).content_value);
  EXPECT_EQ(kArabicDescription, seg->candidate(3).description);

  // "????????????????????? ... ????????????"
  std::string expected4 = "??????";  // "??????"
  for (size_t i = 0; i < 100 / 3; ++i) {
    expected4 += "????????????";
  }
  EXPECT_EQ(expected4, seg->candidate(4).value);
  EXPECT_EQ(expected4, seg->candidate(4).content_value);
  EXPECT_EQ(kArabicDescription, seg->candidate(4).description);

  EXPECT_EQ("Googol", seg->candidate(5).value);
  EXPECT_EQ("Googol", seg->candidate(5).content_value);
  EXPECT_EQ("", seg->candidate(5).description);

  seg->clear_candidates();
}

TEST_F(NumberRewriterTest, RankingForKanjiCandidate) {
  // If kanji candidate is higher before we rewrite segments,
  // kanji should have higher raking.
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  {
    Segment *segment = segments.add_segment();
    DCHECK(segment);
    segment->set_key("???????????????");
    Segment::Candidate *candidate = segment->add_candidate();
    candidate = segment->add_candidate();
    candidate->Init();
    candidate->lid = pos_matcher_.GetNumberId();
    candidate->rid = pos_matcher_.GetNumberId();
    candidate->key = "???????????????";
    candidate->value = "??????";
    candidate->content_value = "??????";
  }

  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));
  EXPECT_NE(0, segments.segments_size());
  int kanji_pos = 0, arabic_pos = 0;
  EXPECT_TRUE(FindCandidateId(segments.segment(0), "??????", &kanji_pos));
  EXPECT_TRUE(FindCandidateId(segments.segment(0), "300", &arabic_pos));
  EXPECT_LT(kanji_pos, arabic_pos);
}

TEST_F(NumberRewriterTest, ModifyExsistingRanking) {
  // Modify exsisting ranking even if the converter returns unusual results
  // due to dictionary noise, etc.
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  {
    Segment *segment = segments.add_segment();
    DCHECK(segment);
    segment->set_key("???????????????");
    Segment::Candidate *candidate = segment->add_candidate();
    candidate->Init();
    candidate->lid = pos_matcher_.GetNumberId();
    candidate->rid = pos_matcher_.GetNumberId();
    candidate->key = "???????????????";
    candidate->value = "??????";
    candidate->content_value = "??????";

    candidate = segment->add_candidate();
    candidate->Init();
    candidate->lid = pos_matcher_.GetNumberId();
    candidate->rid = pos_matcher_.GetNumberId();
    candidate->key = "???????????????";
    candidate->value = "??????";
    candidate->content_value = "??????";
  }

  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));
  int kanji_pos = 0, old_kanji_pos = 0;
  EXPECT_NE(0, segments.segments_size());
  EXPECT_TRUE(FindCandidateId(segments.segment(0), "??????", &kanji_pos));
  EXPECT_TRUE(FindCandidateId(segments.segment(0), "??????", &old_kanji_pos));
  EXPECT_LT(kanji_pos, old_kanji_pos);
}

TEST_F(NumberRewriterTest, EraseExistingCandidates) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  {
    Segment *segment = segments.add_segment();
    DCHECK(segment);
    segment->set_key("??????");
    Segment::Candidate *candidate = segment->add_candidate();
    candidate->Init();
    candidate->lid = pos_matcher_.GetUnknownId();  // Not number POS
    candidate->rid = pos_matcher_.GetUnknownId();
    candidate->key = "??????";
    candidate->content_key = "??????";
    candidate->value = "???";
    candidate->content_value = "???";

    candidate = segment->add_candidate();
    candidate->Init();
    candidate->lid = pos_matcher_.GetNumberId();  // Number POS
    candidate->rid = pos_matcher_.GetNumberId();
    candidate->key = "??????";
    candidate->content_key = "??????";
    candidate->value = "???";
    candidate->content_value = "???";
  }

  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));

  // "???" becomes the base candidate, instead of "???"
  int base_pos = 0;
  EXPECT_TRUE(FindCandidateId(segments.segment(0), "???", &base_pos));
  EXPECT_EQ(0, base_pos);

  // Daiji will be inserted with new correct POS ids.
  int daiji_pos = 0;
  EXPECT_TRUE(FindCandidateId(segments.segment(0), "???", &daiji_pos));
  EXPECT_GT(daiji_pos, 0);
  EXPECT_EQ(pos_matcher_.GetNumberId(),
            segments.segment(0).candidate(daiji_pos).lid);
  EXPECT_EQ(pos_matcher_.GetNumberId(),
            segments.segment(0).candidate(daiji_pos).rid);
}

TEST_F(NumberRewriterTest, SeparatedArabicsTest) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  // Expected data to succeed tests.
  const char *kSuccess[][3] = {
      {"1000", "1,000", "???????????????"},
      {"12345678", "12,345,678", "??????????????????????????????"},
      {"1234.5", "1,234.5", "?????????????????????"},
  };

  for (size_t i = 0; i < std::size(kSuccess); ++i) {
    Segments segments;
    Segment *seg = segments.push_back_segment();
    Segment::Candidate *candidate = seg->add_candidate();
    candidate->Init();
    candidate->lid = pos_matcher_.GetNumberId();
    candidate->rid = pos_matcher_.GetNumberId();
    candidate->value = kSuccess[i][0];
    candidate->content_value = kSuccess[i][0];
    EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));
    EXPECT_TRUE(FindValue(segments.segment(0), kSuccess[i][1]))
        << "Input : " << kSuccess[i][0];
    EXPECT_TRUE(FindValue(segments.segment(0), kSuccess[i][2]))
        << "Input : " << kSuccess[i][0];
  }

  // Expected data to fail tests.
  const char *kFail[][3] = {
      {"123", ",123", "????????????"},
      {"999", ",999", "????????????"},
      {"0000", "0,000", "???????????????"},
  };

  for (size_t i = 0; i < std::size(kFail); ++i) {
    Segments segments;
    Segment *seg = segments.push_back_segment();
    Segment::Candidate *candidate = seg->add_candidate();
    candidate->Init();
    candidate->lid = pos_matcher_.GetNumberId();
    candidate->rid = pos_matcher_.GetNumberId();
    candidate->value = kFail[i][0];
    candidate->content_value = kFail[i][0];
    EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));
    EXPECT_FALSE(FindValue(segments.segment(0), kFail[i][1]))
        << "Input : " << kFail[i][0];
    EXPECT_FALSE(FindValue(segments.segment(0), kFail[i][2]))
        << "Input : " << kFail[i][0];
  }
}

// Consider the case where user dictionaries contain following entry.
// - Reading: "????????????"
// - Value: "8823"
// - POS: GeneralNoun (not *Number*)
// In this case, NumberRewriter should not clear
// Segment::Candidate::USER_DICTIONARY bit in the base candidate.
TEST_F(NumberRewriterTest, PreserveUserDictionaryAttribute) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());
  {
    Segments segments;
    {
      Segment *seg = segments.push_back_segment();
      Segment::Candidate *candidate = seg->add_candidate();
      candidate->Init();
      candidate->lid = pos_matcher_.GetGeneralNounId();
      candidate->rid = pos_matcher_.GetGeneralNounId();
      candidate->key = "????????????";
      candidate->content_key = candidate->key;
      candidate->value = "8823";
      candidate->content_value = candidate->value;
      candidate->cost = 5925;
      candidate->wcost = 5000;
      candidate->attributes = Segment::Candidate::USER_DICTIONARY |
                              Segment::Candidate::NO_VARIANTS_EXPANSION;
    }

    EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));
    bool base_candidate_found = false;
    {
      const Segment &segment = segments.segment(0);
      for (size_t i = 0; i < segment.candidates_size(); ++i) {
        const Segment::Candidate &candidate = segment.candidate(i);
        if (candidate.value == "8823" &&
            (candidate.attributes & Segment::Candidate::USER_DICTIONARY)) {
          base_candidate_found = true;
          break;
        }
      }
    }
    EXPECT_TRUE(base_candidate_found);
  }
}

TEST_F(NumberRewriterTest, DuplicateCandidateTest) {
  // To reproduce issue b/6714268.
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());
  ConversionRequest convreq;
  commands::Request request;
  convreq.set_request(&request);
  std::unique_ptr<NumberRewriter> rewriter(CreateNumberRewriter());

  {
    request.set_mixed_conversion(true);
    EXPECT_EQ(RewriterInterface::ALL, rewriter->capability(convreq));
  }

  {
    request.set_mixed_conversion(false);
    EXPECT_EQ(RewriterInterface::CONVERSION, rewriter->capability(convreq));
  }
}

TEST_F(NumberRewriterTest, NonNumberNounTest) {
  // Test if "?????????" is not rewritten to "100??????", etc.
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());
  Segments segments;
  Segment *segment = segments.push_back_segment();
  segment->set_key("");
  Segment::Candidate *cand = segment->add_candidate();
  cand->Init();
  cand->key = "??????";
  cand->content_key = cand->key;
  cand->value = "?????????";
  cand->content_value = cand->value;
  cand->lid = pos_matcher_.GetGeneralNounId();
  cand->rid = pos_matcher_.GetGeneralNounId();
  EXPECT_FALSE(number_rewriter->Rewrite(default_request_, &segments));
}

TEST_F(NumberRewriterTest, RewriteArabicNumberTest) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());
  Segments segments;
  Segment *segment = segments.push_back_segment();
  segment->set_key("??????");
  struct CandData {
    const char *value;
    int pos_id;
  };
  const CandData kCandList[] = {
      {"1", pos_matcher_.GetNumberId()},
      {"???", pos_matcher_.GetNumberId()},
      {"??????", pos_matcher_.GetGeneralNounId()},
      {"??????", pos_matcher_.GetGeneralNounId()},
      {"???", pos_matcher_.GetGeneralNounId()},
  };

  for (const auto &cand_data : kCandList) {
    Segment::Candidate *cand = segment->add_candidate();
    cand->Init();
    cand->key = "??????";
    cand->content_key = cand->key;
    cand->value = cand_data.value;
    cand->content_value = cand->value;
    cand->lid = cand_data.pos_id;
    cand->rid = cand_data.pos_id;
  }
  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));
  EXPECT_GT(segment->candidates_size(), 1);
  EXPECT_EQ("???", segment->candidate(0).value);
  EXPECT_EQ("??????", segment->candidate(1).value);
}

TEST_F(NumberRewriterTest, RewriteForPartialSuggestion_b16765535) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  constexpr char kBubun[] = "??????";
  Segments segments;
  {
    Segment *seg = segments.push_back_segment();
    Segment::Candidate *candidate = seg->add_candidate();
    candidate->Init();
    candidate->lid = pos_matcher_.GetNumberId();
    candidate->rid = pos_matcher_.GetNumberId();
    candidate->key = "090";
    candidate->value = "090";
    candidate->content_key = "090";
    candidate->content_value = "090";
    candidate->description = kBubun;
    candidate->attributes = Segment::Candidate::PARTIALLY_KEY_CONSUMED;
    candidate->consumed_key_size = 3;
  }
  {
    Segment *seg = segments.push_back_segment();
    Segment::Candidate *candidate = seg->add_candidate();
    candidate->Init();
    candidate->key = "-";
    candidate->value = "-";
    candidate->content_key = "-";
    candidate->content_value = "-";
  }
  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));

  ASSERT_EQ(2, segments.conversion_segments_size());
  const Segment &seg = segments.conversion_segment(0);
  ASSERT_LE(2, seg.candidates_size());
  for (size_t i = 0; i < seg.candidates_size(); ++i) {
    const Segment::Candidate &candidate = seg.candidate(i);
    EXPECT_TRUE(absl::StartsWith(candidate.description, kBubun));
    EXPECT_TRUE(candidate.attributes &
                Segment::Candidate::PARTIALLY_KEY_CONSUMED);
  }
}

TEST_F(NumberRewriterTest, RewriteForPartialSuggestion_b19470020) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  constexpr char kBubun[] = "??????";
  Segments segments;
  {
    Segment *seg = segments.push_back_segment();
    seg->set_key("????????????????????????");
    Segment::Candidate *candidate = seg->add_candidate();
    candidate->Init();
    candidate->lid = pos_matcher_.GetNumberId();
    candidate->rid = pos_matcher_.GetNumberId();
    candidate->key = "?????????";
    candidate->value = "??????";
    candidate->content_key = "?????????";
    candidate->content_value = "??????";
    candidate->description = kBubun;
    candidate->attributes = Segment::Candidate::PARTIALLY_KEY_CONSUMED;
    candidate->consumed_key_size = 3;
  }
  EXPECT_TRUE(number_rewriter->Rewrite(default_request_, &segments));

  ASSERT_EQ(1, segments.conversion_segments_size());
  const Segment &seg = segments.conversion_segment(0);
  ASSERT_LE(2, seg.candidates_size());
  bool found_halfwidth = false;
  for (size_t i = 0; i < seg.candidates_size(); ++i) {
    const Segment::Candidate &candidate = seg.candidate(i);
    if (candidate.value != "1???") {
      continue;
    }
    found_halfwidth = true;
    EXPECT_EQ(3, candidate.consumed_key_size);
    EXPECT_TRUE(absl::StartsWith(candidate.description, kBubun));
    EXPECT_TRUE(candidate.attributes &
                Segment::Candidate::PARTIALLY_KEY_CONSUMED);
  }
  EXPECT_TRUE(found_halfwidth);
}

TEST_F(NumberRewriterTest, RewritePhonePrefix_b16668386) {
  std::unique_ptr<NumberRewriter> number_rewriter(CreateNumberRewriter());

  Segments segments;
  Segment *seg = segments.push_back_segment();
  Segment::Candidate *candidate = seg->add_candidate();
  candidate->Init();
  candidate->lid = pos_matcher_.GetNumberId();
  candidate->rid = pos_matcher_.GetGeneralSymbolId();
  candidate->key = "090-";
  candidate->value = "090-";
  candidate->content_key = "090-";
  candidate->content_value = "090-";

  EXPECT_FALSE(number_rewriter->Rewrite(default_request_, &segments));
}

}  // namespace mozc
