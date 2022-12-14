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

#include "win32/base/string_util.h"
#include <string>
#include "protocol/commands.pb.h"
#include "testing/base/public/googletest.h"
#include "testing/base/public/gunit.h"

namespace mozc {
namespace win32 {

TEST(StringUtilTest, InvalidCases) {
  EXPECT_EQ("", StringUtil::KeyToReadingA(""));
  // KeyToReadingA fails if the resultant string is longer than 512 characters.
  std::string longa(10000, 'a');
  EXPECT_EQ("", StringUtil::KeyToReadingA(longa));
}

TEST(StringUtilTest, Hiragana) {
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
}

TEST(StringUtilTest, Katakana) {
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("??????", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
}

TEST(StringUtilTest, AlphaNumeric) {
  EXPECT_EQ("!", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("\"", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("#", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("$", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("%", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("&", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("'", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("(", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ(")", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("=", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("-", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("~", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("^", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("|", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("\\", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("`", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("@", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("{", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("+", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ(";", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("*", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ(":", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("}", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("<", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ(">", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("?", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("???", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("_", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("1", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("2", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("3", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("4", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("5", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("6", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("7", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("8", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("9", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("0", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("a", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("b", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("c", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("d", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("e", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("f", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("g", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("h", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("i", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("j", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("k", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("l", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("m", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("n", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("o", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("p", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("q", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("r", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("s", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("t", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("u", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("v", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("w", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("x", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("y", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("z", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("A", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("B", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("C", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("D", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("E", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("F", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("G", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("H", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("I", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("J", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("K", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("L", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("M", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("N", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("O", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("P", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("Q", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("R", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("S", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("T", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("U", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("V", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("W", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("X", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("Y", StringUtil::KeyToReadingA("???"));
  EXPECT_EQ("Z", StringUtil::KeyToReadingA("???"));
}

TEST(StringUtilTest, LCMapStringATest) {
  DWORD lcid =
      MAKELCID(MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT), SORT_JAPANESE_XJIS);

  char buf[512];
  // backquote
  size_t len =
      LCMapStringA(lcid, LCMAP_HALFWIDTH, "\x81\x65", -1, buf, sizeof(buf));
  EXPECT_EQ(2, len);
  // LCMapStringA converts "\x81\x65" (backquote) to ' for some reason.
  // EXPECT_EQ('`', buf[0]);
  EXPECT_EQ('\'', buf[0]);

  // quote
  len = LCMapStringA(lcid, LCMAP_HALFWIDTH, "\x81\x66", -1, buf, sizeof(buf));
  EXPECT_EQ(2, len);
  EXPECT_EQ('\'', buf[0]);
}

TEST(StringUtilTest, ComposePreeditText) {
  commands::Preedit preedit;
  preedit.add_segment()->set_value("a");
  preedit.add_segment()->set_value("b");
  preedit.add_segment()->set_value("c");
  EXPECT_EQ(L"abc", StringUtil::ComposePreeditText(preedit));
}

}  // namespace win32
}  // namespace mozc
