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

#include "rewriter/focus_candidate_rewriter.h"

#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "base/number_util.h"
#include "base/system_util.h"
#include "config/config_handler.h"
#include "converter/segments.h"
#include "data_manager/testing/mock_data_manager.h"
#include "testing/base/public/googletest.h"
#include "testing/base/public/gunit.h"
#include "transliteration/transliteration.h"
#include "absl/flags/flag.h"

namespace mozc {
namespace {

void AddCandidate(Segment *segment, const std::string &value) {
  Segment::Candidate *c = segment->add_candidate();
  c->Init();
  c->value = value;
  c->content_value = value;
}

void AddCandidateWithContentValue(Segment *segment, const std::string &value,
                                  const std::string &content_value) {
  Segment::Candidate *c = segment->add_candidate();
  c->Init();
  c->value = value;
  c->content_value = content_value;
}

}  // namespace

class FocusCandidateRewriterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SystemUtil::SetUserProfileDirectory(absl::GetFlag(FLAGS_test_tmpdir));
    rewriter_ = std::make_unique<FocusCandidateRewriter>(&mock_data_manager_);
  }

  const RewriterInterface *GetRewriter() { return rewriter_.get(); }

 private:
  std::unique_ptr<FocusCandidateRewriter> rewriter_;
  testing::MockDataManager mock_data_manager_;
};

TEST_F(FocusCandidateRewriterTest, FocusCandidateRewriterInvalidQuery) {
  Segments segments;

  Segment *seg1 = segments.add_segment();
  Segment *seg2 = segments.add_segment();
  Segment *seg3 = segments.add_segment();
  Segment *seg4 = segments.add_segment();

  AddCandidate(seg1, "???");
  AddCandidate(seg1, "(");
  AddCandidate(seg1, "[");
  AddCandidate(seg1, "{");
  AddCandidate(seg2, "?????????");
  AddCandidate(seg2, "?????????");
  AddCandidate(seg3, "??????");
  AddCandidate(seg3, "??????");
  AddCandidate(seg4, "???");
  AddCandidate(seg4, ")");
  AddCandidate(seg4, "]");
  AddCandidate(seg4, "}");

  // invalid queries
  EXPECT_FALSE(GetRewriter()->Focus(&segments, 5, 0));
  EXPECT_FALSE(GetRewriter()->Focus(&segments, 0, 10));
  EXPECT_FALSE(GetRewriter()->Focus(&segments, 1, 0));
  EXPECT_FALSE(GetRewriter()->Focus(&segments, 2, 0));
}

TEST_F(FocusCandidateRewriterTest, FocusCandidateRewriterLeftToRight) {
  Segments segments;

  Segment *seg1 = segments.add_segment();
  Segment *seg2 = segments.add_segment();
  Segment *seg3 = segments.add_segment();
  Segment *seg4 = segments.add_segment();

  AddCandidate(seg1, "???");
  AddCandidate(seg1, "(");
  AddCandidate(seg1, "[");
  AddCandidate(seg1, "{");
  AddCandidate(seg2, "?????????");
  AddCandidate(seg2, "?????????");
  AddCandidate(seg3, "??????");
  AddCandidate(seg3, "??????");
  AddCandidate(seg4, "???");
  AddCandidate(seg4, ")");
  AddCandidate(seg4, "]");
  AddCandidate(seg4, "}");

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 0));
  EXPECT_EQ("???", seg4->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 1));
  EXPECT_EQ(")", seg4->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 2));
  EXPECT_EQ("]", seg4->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 3));
  EXPECT_EQ("}", seg4->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 0));
  EXPECT_EQ("???", seg4->candidate(0).content_value);
}

TEST_F(FocusCandidateRewriterTest, FocusCandidateRewriterRightToLeft) {
  Segments segments;

  Segment *seg1 = segments.add_segment();
  Segment *seg2 = segments.add_segment();
  Segment *seg3 = segments.add_segment();
  Segment *seg4 = segments.add_segment();

  AddCandidate(seg1, "???");
  AddCandidate(seg1, "(");
  AddCandidate(seg1, "[");
  AddCandidate(seg1, "{");
  AddCandidate(seg2, "?????????");
  AddCandidate(seg2, "?????????");
  AddCandidate(seg3, "??????");
  AddCandidate(seg3, "??????");
  AddCandidate(seg4, "???");
  AddCandidate(seg4, ")");
  AddCandidate(seg4, "]");
  AddCandidate(seg4, "}");

  // right to left
  EXPECT_TRUE(GetRewriter()->Focus(&segments, 3, 0));
  EXPECT_EQ("???", seg1->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 3, 1));
  EXPECT_EQ("(", seg1->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 3, 2));
  EXPECT_EQ("[", seg1->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 3, 3));
  EXPECT_EQ("{", seg1->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 3, 0));
  EXPECT_EQ("???", seg1->candidate(0).content_value);
}

TEST_F(FocusCandidateRewriterTest, FocusCandidateRewriterLeftToRightNest) {
  Segments segments;
  Segment *seg[7];
  for (int i = 0; i < std::size(seg); ++i) {
    seg[i] = segments.add_segment();
  }

  AddCandidate(seg[0], "???");
  AddCandidate(seg[0], "(");
  AddCandidate(seg[0], "[");
  AddCandidate(seg[0], "{");
  AddCandidate(seg[1], "?????????1");
  AddCandidate(seg[2], "???");
  AddCandidate(seg[2], "(");
  AddCandidate(seg[2], "[");
  AddCandidate(seg[2], "{");
  AddCandidate(seg[3], "?????????2");
  AddCandidate(seg[4], "???");
  AddCandidate(seg[4], ")");
  AddCandidate(seg[4], "]");
  AddCandidate(seg[4], "}");
  AddCandidate(seg[5], "?????????3");
  AddCandidate(seg[6], "???");
  AddCandidate(seg[6], ")");
  AddCandidate(seg[6], "]");
  AddCandidate(seg[6], "}");

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 0));
  EXPECT_EQ("???", seg[6]->candidate(0).content_value);
  EXPECT_EQ("???", seg[4]->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 1));
  EXPECT_EQ(")", seg[6]->candidate(0).content_value);
  EXPECT_EQ("???", seg[4]->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 2, 0));
  EXPECT_EQ(")", seg[6]->candidate(0).content_value);
  EXPECT_EQ("???", seg[4]->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 2, 1));
  EXPECT_EQ(")", seg[6]->candidate(0).content_value);
  EXPECT_EQ(")", seg[4]->candidate(0).content_value);
}

TEST_F(FocusCandidateRewriterTest, FocusCandidateRewriterRightToLeftNest) {
  Segments segments;
  Segment *seg[7];
  for (int i = 0; i < std::size(seg); ++i) {
    seg[i] = segments.add_segment();
  }

  AddCandidate(seg[0], "???");
  AddCandidate(seg[0], "(");
  AddCandidate(seg[0], "[");
  AddCandidate(seg[0], "{");
  AddCandidate(seg[1], "?????????1");
  AddCandidate(seg[2], "???");
  AddCandidate(seg[2], "(");
  AddCandidate(seg[2], "[");
  AddCandidate(seg[2], "{");
  AddCandidate(seg[3], "?????????2");
  AddCandidate(seg[4], "???");
  AddCandidate(seg[4], ")");
  AddCandidate(seg[4], "]");
  AddCandidate(seg[4], "}");
  AddCandidate(seg[5], "?????????3");
  AddCandidate(seg[6], "???");
  AddCandidate(seg[6], ")");
  AddCandidate(seg[6], "]");
  AddCandidate(seg[6], "}");

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 6, 0));
  EXPECT_EQ("???", seg[0]->candidate(0).content_value);
  EXPECT_EQ("???", seg[2]->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 6, 1));
  EXPECT_EQ("(", seg[0]->candidate(0).content_value);
  EXPECT_EQ("???", seg[2]->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 4, 0));
  EXPECT_EQ("(", seg[0]->candidate(0).content_value);
  EXPECT_EQ("???", seg[2]->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 4, 1));
  EXPECT_EQ("(", seg[0]->candidate(0).content_value);
  EXPECT_EQ("(", seg[2]->candidate(0).content_value);
}

TEST_F(FocusCandidateRewriterTest, FocusCandidateRewriterMetaCandidate) {
  Segments segments;
  Segment *seg[3];
  for (int i = 0; i < std::size(seg); ++i) {
    seg[i] = segments.add_segment();
  }

  // set key
  seg[0]->set_key("???");
  // set meta candidates
  {
    EXPECT_EQ(0, seg[0]->meta_candidates_size());
    std::vector<Segment::Candidate> *meta_cands =
        seg[0]->mutable_meta_candidates();
    meta_cands->resize(transliteration::NUM_T13N_TYPES);
    for (size_t i = 0; i < transliteration::NUM_T13N_TYPES; ++i) {
      meta_cands->at(i).Init();
      meta_cands->at(i).value = "???";
      meta_cands->at(i).content_value = "???";
    }
    meta_cands->at(transliteration::HALF_KATAKANA).value = "???";
    meta_cands->at(transliteration::HALF_KATAKANA).content_value = "???";
    EXPECT_EQ(transliteration::NUM_T13N_TYPES, seg[0]->meta_candidates_size());
  }
  EXPECT_EQ(
      "???",
      seg[0]->meta_candidate(transliteration::HALF_KATAKANA).content_value);
  AddCandidate(seg[0], "???");
  AddCandidate(seg[0], "(");
  AddCandidate(seg[0], "[");
  AddCandidate(seg[0], "{");

  AddCandidate(seg[1], "?????????1");

  // set key
  seg[2]->set_key("???");
  // set meta candidates
  {
    EXPECT_EQ(0, seg[2]->meta_candidates_size());
    std::vector<Segment::Candidate> *meta_cands =
        seg[2]->mutable_meta_candidates();
    meta_cands->resize(transliteration::NUM_T13N_TYPES);
    for (size_t i = 0; i < transliteration::NUM_T13N_TYPES; ++i) {
      meta_cands->at(i).Init();
      meta_cands->at(i).value = "???";
      meta_cands->at(i).content_value = "???";
    }
    meta_cands->at(transliteration::HALF_KATAKANA).value = "???";
    meta_cands->at(transliteration::HALF_KATAKANA).content_value = "???";
    EXPECT_EQ(transliteration::NUM_T13N_TYPES, seg[2]->meta_candidates_size());
  }
  EXPECT_EQ(
      "???",
      seg[2]->meta_candidate(transliteration::HALF_KATAKANA).content_value);
  AddCandidate(seg[2], "???");
  AddCandidate(seg[2], ")");
  AddCandidate(seg[2], "]");
  AddCandidate(seg[2], "}");

  const int half_index = -transliteration::HALF_KATAKANA - 1;
  EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, half_index));
  EXPECT_EQ("???", seg[0]->candidate(0).content_value);
  EXPECT_EQ("???", seg[2]->candidate(0).content_value);

  const int valid_index = -(transliteration::NUM_T13N_TYPES - 1) - 1;
  EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, valid_index));
  const int invalid_index = -transliteration::NUM_T13N_TYPES - 1;
  EXPECT_FALSE(GetRewriter()->Focus(&segments, 0, invalid_index));
}

TEST_F(FocusCandidateRewriterTest, FocusCandidateRewriterNumber) {
  Segments segments;
  Segment *seg[7];
  for (int i = 0; i < std::size(seg); ++i) {
    seg[i] = segments.add_segment();
  }

  // set key
  seg[0]->set_key("2");
  AddCandidate(seg[0], "2");
  AddCandidate(seg[0], "???");
  AddCandidate(seg[0], "???");
  AddCandidate(seg[0], "???");

  seg[0]->mutable_candidate(2)->style = NumberUtil::NumberString::NUMBER_KANJI;
  seg[0]->mutable_candidate(3)->style =
      NumberUtil::NumberString::NUMBER_OLD_KANJI;

  AddCandidate(seg[1], "?????????1");

  // set key
  seg[2]->set_key("3");
  AddCandidate(seg[2], "3");
  AddCandidate(seg[2], "???");
  AddCandidate(seg[2], "???");
  AddCandidate(seg[2], "???");

  seg[2]->mutable_candidate(2)->style = NumberUtil::NumberString::NUMBER_KANJI;
  seg[2]->mutable_candidate(3)->style =
      NumberUtil::NumberString::NUMBER_OLD_KANJI;

  seg[3]->set_key("4");
  AddCandidate(seg[3], "4");
  AddCandidate(seg[3], "???");
  AddCandidate(seg[3], "???");
  seg[3]->mutable_candidate(2)->style = NumberUtil::NumberString::NUMBER_KANJI;

  AddCandidate(seg[4], "?????????1");
  AddCandidate(seg[5], "?????????1");

  seg[6]->set_key("4");
  AddCandidate(seg[6], "4");
  AddCandidate(seg[6], "???");
  AddCandidate(seg[6], "???");

  seg[6]->mutable_candidate(2)->style = NumberUtil::NumberString::NUMBER_KANJI;

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 0));
  EXPECT_EQ("2", seg[0]->candidate(0).content_value);
  EXPECT_EQ("3", seg[2]->candidate(0).content_value);
  EXPECT_EQ("4", seg[3]->candidate(0).content_value);

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 1));
  EXPECT_EQ("???", seg[2]->candidate(0).content_value);
  EXPECT_EQ("???", seg[3]->candidate(0).content_value);

  EXPECT_EQ("4", seg[6]->candidate(0).content_value);  // far from

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 2));
  EXPECT_EQ("???", seg[2]->candidate(0).content_value);
  EXPECT_EQ("???", seg[3]->candidate(0).content_value);

  EXPECT_EQ("4", seg[6]->candidate(0).content_value);  // far from

  EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 3));
  EXPECT_EQ("???", seg[2]->candidate(0).content_value);
  EXPECT_EQ("4", seg[6]->candidate(0).content_value);  // far from
}

// Bug #4596846: Non-number characters are changed to numbers
TEST_F(FocusCandidateRewriterTest, DontChangeNonNumberSegment) {
  Segments segments;
  Segment *seg[2];
  for (int i = 0; i < std::size(seg); ++i) {
    seg[i] = segments.add_segment();
  }

  // set key
  seg[0]->set_key("1");
  AddCandidate(seg[0], "1");
  AddCandidate(seg[0], "???");
  AddCandidate(seg[1], "???");
  AddCandidate(seg[1], "???");

  // Should not change a segment that doesn't have a number as its first
  // candidate.
  EXPECT_FALSE(GetRewriter()->Focus(&segments, 0, 1));
  EXPECT_NE("???", seg[1]->candidate(0).content_value);
}

TEST_F(FocusCandidateRewriterTest, FocusCandidateRewriterSuffix) {
  {
    Segments segments;
    Segment *seg[6];
    for (int i = 0; i < std::size(seg); ++i) {
      seg[i] = segments.add_segment();
    }

    seg[0]->set_key("2");
    AddCandidate(seg[0], "2");

    seg[1]->set_key("??????");
    AddCandidate(seg[1], "???");
    AddCandidate(seg[1], "???");

    seg[2]->set_key("3");
    AddCandidate(seg[2], "3");

    seg[3]->set_key("??????");
    AddCandidate(seg[3], "???");
    AddCandidate(seg[3], "???");

    seg[4]->set_key("4");
    AddCandidate(seg[4], "4");

    seg[5]->set_key("??????");
    AddCandidate(seg[5], "???");
    AddCandidate(seg[5], "???");

    EXPECT_TRUE(GetRewriter()->Focus(&segments, 1, 1));
    EXPECT_EQ("???", seg[3]->candidate(0).content_value);
    EXPECT_EQ("???", seg[5]->candidate(0).content_value);
  }

  // No Number
  {
    Segments segments;
    Segment *seg[3];
    for (int i = 0; i < std::size(seg); ++i) {
      seg[i] = segments.add_segment();
    }

    seg[0]->set_key("??????");
    AddCandidate(seg[0], "???");
    AddCandidate(seg[0], "???");

    seg[1]->set_key("3");
    AddCandidate(seg[1], "3");

    seg[2]->set_key("??????");
    AddCandidate(seg[2], "???");
    AddCandidate(seg[2], "???");
    EXPECT_FALSE(GetRewriter()->Focus(&segments, 0, 1));
  }

  // No Number
  {
    Segments segments;
    Segment *seg[3];
    for (int i = 0; i < std::size(seg); ++i) {
      seg[i] = segments.add_segment();
    }

    seg[0]->set_key("??????");
    AddCandidate(seg[0], "???");
    AddCandidate(seg[0], "???");

    seg[1]->set_key("3");
    AddCandidate(seg[1], "3");

    seg[2]->set_key("??????");
    AddCandidate(seg[2], "???");
    AddCandidate(seg[2], "???");
    EXPECT_FALSE(GetRewriter()->Focus(&segments, 0, 1));
  }

  // No number
  {
    Segments segments;
    Segment *seg[3];
    for (int i = 0; i < std::size(seg); ++i) {
      seg[i] = segments.add_segment();
    }

    seg[0]->set_key("2");
    AddCandidate(seg[0], "2");

    seg[1]->set_key("??????");
    AddCandidate(seg[1], "???");
    AddCandidate(seg[1], "???");
    seg[2]->set_key("??????");
    AddCandidate(seg[2], "???");
    AddCandidate(seg[2], "???");

    EXPECT_FALSE(GetRewriter()->Focus(&segments, 1, 1));
  }
}

TEST_F(FocusCandidateRewriterTest, NumberAndSuffixCompound) {
  // Test for reranking of number compound pattern:
  {
    // Test scenario: Construct the following segments:
    //           Seg 0       Seg 1
    //        | "????????????" | "?????????" |
    // cand 0 | "??????"    | "2???"   |
    // cand 1 | "??????"    | "??????"   |
    // cand 2 |          | "??????"   |
    //
    // Then, focusing on (Seg 0, cand 1) should move "??????" in Seg 1 to the top.
    Segments segments;
    Segment *seg[2];
    for (int i = 0; i < std::size(seg); ++i) {
      seg[i] = segments.add_segment();
    }
    seg[0]->set_key("????????????");
    AddCandidate(seg[0], "??????");
    AddCandidate(seg[0], "??????");

    seg[1]->set_key("?????????");
    AddCandidate(seg[1], "2???");
    AddCandidate(seg[1], "??????");
    AddCandidate(seg[1], "??????");

    EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 1));
    EXPECT_EQ("??????", seg[1]->candidate(0).value);
  }
  // Test for reranking of number compound + parallel marker pattern:
  // http://mozcsuorg.appspot.com/#issue/49
  {
    // Test scenario: Similar to the above case, but construct the following
    // segments with parallel marker:
    //           Seg 0       Seg 1
    //        | "???????????????" | "?????????" |
    // cand 0 | "?????????"    | "2???"    |
    // cand 1 | "?????????"    | "??????"   |
    // cand 2 |            | "??????"   |
    //
    // Then, focusing on (Seg 0, cand 1) should move "??????" in Seg 1 to the top.
    Segments segments;
    Segment *seg[2];
    for (int i = 0; i < std::size(seg); ++i) {
      seg[i] = segments.add_segment();
    }

    constexpr int kNounId = 1939;
    constexpr int kParallelMarkerYa = 290;

    seg[0]->set_key("???????????????");

    AddCandidateWithContentValue(seg[0], "?????????", "??????");
    seg[0]->mutable_candidate(0)->lid = kNounId;
    seg[0]->mutable_candidate(0)->rid = kParallelMarkerYa;

    AddCandidateWithContentValue(seg[0], "?????????", "??????");
    seg[0]->mutable_candidate(1)->lid = kNounId;
    seg[0]->mutable_candidate(1)->rid = kParallelMarkerYa;

    seg[1]->set_key("?????????");
    AddCandidate(seg[1], "2???");
    seg[1]->mutable_candidate(0)->lid = kNounId;
    seg[1]->mutable_candidate(0)->rid = kNounId;
    AddCandidate(seg[1], "??????");
    seg[1]->mutable_candidate(1)->lid = kNounId;
    seg[1]->mutable_candidate(1)->rid = kNounId;
    AddCandidate(seg[1], "??????");
    seg[1]->mutable_candidate(2)->lid = kNounId;
    seg[1]->mutable_candidate(2)->rid = kNounId;

    EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 1));
    EXPECT_EQ("??????", seg[1]->candidate(0).value);
  }
  // Test for reranking of number compound + parallel marker pattern for 3
  // segments.
  {
    // Test scenario: Similar to the above case, but construct the following
    // segments with parallel marker:
    //           Seg 0       Seg 1      Seg 2
    //        | "???????????????" | "????????????" | "????????????"
    // cand 0 | "?????????"    | "2??????"    | "??????"
    // cand 1 | "?????????"    | "?????????"   | "3???"
    // cand 2 |            | "?????????"   | "??????"
    //
    // Then, focusing on (Seg 0, cand 1) should move "?????????" in Seg 1 and "??????
    // " in Seg 2 to the top of each segment.
    Segments segments;
    Segment *seg[3];
    for (int i = 0; i < std::size(seg); ++i) {
      seg[i] = segments.add_segment();
    }

    constexpr int kNounId = 1939;
    constexpr int kParallelMarkerYa = 290;

    seg[0]->set_key("???????????????");

    AddCandidateWithContentValue(seg[0], "?????????", "??????");
    seg[0]->mutable_candidate(0)->lid = kNounId;
    seg[0]->mutable_candidate(0)->rid = kParallelMarkerYa;

    AddCandidateWithContentValue(seg[0], "?????????", "??????");
    seg[0]->mutable_candidate(1)->lid = kNounId;
    seg[0]->mutable_candidate(1)->rid = kParallelMarkerYa;

    seg[1]->set_key("????????????");
    AddCandidateWithContentValue(seg[1], "2??????", "2???");
    seg[1]->mutable_candidate(0)->lid = kNounId;
    seg[1]->mutable_candidate(0)->rid = kParallelMarkerYa;
    AddCandidateWithContentValue(seg[1], "?????????", "??????");
    seg[1]->mutable_candidate(1)->lid = kNounId;
    seg[1]->mutable_candidate(1)->rid = kParallelMarkerYa;
    AddCandidateWithContentValue(seg[1], "?????????", "??????");
    seg[1]->mutable_candidate(2)->lid = kNounId;
    seg[1]->mutable_candidate(2)->rid = kParallelMarkerYa;

    seg[2]->set_key("???????????????");
    AddCandidate(seg[2], "??????");
    seg[2]->mutable_candidate(0)->lid = kNounId;
    seg[2]->mutable_candidate(0)->rid = kNounId;
    AddCandidate(seg[2], "3???");
    seg[2]->mutable_candidate(1)->lid = kNounId;
    seg[2]->mutable_candidate(1)->rid = kNounId;
    AddCandidate(seg[2], "??????");
    seg[2]->mutable_candidate(2)->lid = kNounId;
    seg[2]->mutable_candidate(2)->rid = kNounId;

    EXPECT_TRUE(GetRewriter()->Focus(&segments, 0, 1));
    EXPECT_EQ("?????????", seg[1]->candidate(0).value);
    EXPECT_EQ("??????", seg[2]->candidate(0).value);
  }
  // Test case where two number segments are too far to be rewritten.
  {
    // Test scenario: Similar to the above case, but construct the following
    // segments with parallel marker:
    //           Seg 0       Seg 1 Seg 2   Seg 3 Seg 4
    //        | "???????????????" | "???" | "???" | "???" | "?????????"
    // cand 0 | "?????????"    | "???" | "???" | "???" | "2???"
    // cand 1 | "?????????"    |      |     |      | "??????"
    //
    // Then, focusing on (Seg 0, cand 1) cannot move "??????" in Seg 3 to the top.
    Segments segments;
    Segment *seg[5];
    for (int i = 0; i < std::size(seg); ++i) {
      seg[i] = segments.add_segment();
    }

    constexpr int kNounId = 1939;
    constexpr int kParallelMarkerYa = 290;

    seg[0]->set_key("???????????????");

    AddCandidateWithContentValue(seg[0], "?????????", "??????");
    seg[0]->mutable_candidate(0)->lid = kNounId;
    seg[0]->mutable_candidate(0)->rid = kParallelMarkerYa;

    AddCandidateWithContentValue(seg[0], "?????????", "??????");
    seg[0]->mutable_candidate(1)->lid = kNounId;
    seg[0]->mutable_candidate(1)->rid = kParallelMarkerYa;

    for (size_t i = 1; i <= 3; ++i) {
      seg[i]->set_key("???");
      AddCandidate(seg[i], "???");
      seg[i]->mutable_candidate(0)->lid = kNounId;
      seg[i]->mutable_candidate(0)->rid = kNounId;
    }

    seg[4]->set_key("?????????");
    AddCandidate(seg[4], "2???");
    seg[4]->mutable_candidate(0)->lid = kNounId;
    seg[4]->mutable_candidate(0)->rid = kNounId;
    AddCandidate(seg[4], "??????");
    seg[4]->mutable_candidate(0)->lid = kNounId;
    seg[4]->mutable_candidate(0)->rid = kNounId;

    EXPECT_FALSE(GetRewriter()->Focus(&segments, 0, 1));
  }
  // Test for the case where we shouldn't rewrite.
  {
    // Test scenario: Similar to the above cases, but construct the following
    // segments with particles ??? and ???:
    //           Seg 0       Seg 1        Seg 2
    //        | "??????????????????" | "?????????" | "?????????"
    // cand 0 | "????????????"    | "??????"   | "?????????"
    // cand 1 | "????????????"    | "??????"   |
    // cand 2 |              | "2???"   |
    //
    // Here, "????????????" has the following structure:
    // "??????(noun)" + "???(?????????)" + "???(?????????)"
    // For this case, focusing on (Seg 0, cand 1) should not move "??????" in Seg
    // 1 to the top.
    Segments segments;
    Segment *seg[3];
    for (int i = 0; i < std::size(seg); ++i) {
      seg[i] = segments.add_segment();
    }

    constexpr int kNounId = 1939;
    constexpr int kKakariJoshiHa = 299;
    constexpr int kIkuTaSetsuzoku = 1501;
    constexpr int kJodoushiTa = 161;

    seg[0]->set_key("??????????????????");

    AddCandidateWithContentValue(seg[0], "????????????", "??????");
    seg[0]->mutable_candidate(0)->lid = kNounId;
    seg[0]->mutable_candidate(0)->rid = kKakariJoshiHa;

    AddCandidateWithContentValue(seg[0], "????????????", "??????");
    seg[0]->mutable_candidate(1)->lid = kNounId;
    seg[0]->mutable_candidate(1)->rid = kKakariJoshiHa;

    seg[1]->set_key("?????????");
    AddCandidate(seg[1], "??????");
    seg[1]->mutable_candidate(0)->lid = kNounId;
    seg[1]->mutable_candidate(0)->rid = kNounId;
    AddCandidate(seg[1], "??????");
    seg[1]->mutable_candidate(1)->lid = kNounId;
    seg[1]->mutable_candidate(1)->rid = kNounId;
    AddCandidate(seg[1], "2???");
    seg[1]->mutable_candidate(2)->lid = kNounId;
    seg[1]->mutable_candidate(2)->rid = kNounId;

    seg[2]->set_key("?????????");
    AddCandidate(seg[2], "?????????");
    seg[2]->mutable_candidate(0)->lid = kIkuTaSetsuzoku;
    seg[2]->mutable_candidate(0)->rid = kJodoushiTa;

    EXPECT_FALSE(GetRewriter()->Focus(&segments, 0, 1));
  }
}

}  // namespace mozc
