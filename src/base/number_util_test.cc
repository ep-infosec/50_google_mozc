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

#include "base/number_util.h"

#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "base/port.h"
#include "testing/base/public/googletest.h"
#include "testing/base/public/gunit.h"

namespace mozc {
namespace {

TEST(NumberUtilTest, SimpleAtoi) {
  EXPECT_EQ(0, NumberUtil::SimpleAtoi("0"));
  EXPECT_EQ(123, NumberUtil::SimpleAtoi("123"));
  EXPECT_EQ(-1, NumberUtil::SimpleAtoi("-1"));

  // Invalid cases return 0.
  EXPECT_EQ(0, NumberUtil::SimpleAtoi("abc"));
  EXPECT_EQ(0, NumberUtil::SimpleAtoi("a1"));
  EXPECT_EQ(0, NumberUtil::SimpleAtoi("1 a"));
}

TEST(NumberUtilTest, SafeStrToInt16) {
  int16_t value = 0x4321;

  EXPECT_TRUE(NumberUtil::SafeStrToInt16("0", &value));
  EXPECT_EQ(0, value);
  value = 0x4321;
  EXPECT_TRUE(NumberUtil::SafeStrToInt16("+0", &value));
  EXPECT_EQ(0, value);
  value = 0x4321;
  EXPECT_TRUE(NumberUtil::SafeStrToInt16("-0", &value));
  EXPECT_EQ(0, value);
  value = 0x4321;
  EXPECT_TRUE(NumberUtil::SafeStrToInt16(" \t\r\n\v\f0 \t\r\n\v\f", &value));
  EXPECT_EQ(0, value);
  value = 0x4321;
  EXPECT_TRUE(NumberUtil::SafeStrToInt16(" \t\r\n\v\f-0 \t\r\n\v\f", &value));
  EXPECT_EQ(0, value);
  value = 0x4321;
  EXPECT_TRUE(NumberUtil::SafeStrToInt16("012345", &value));
  EXPECT_EQ(12345, value);
  value = 0x4321;
  EXPECT_TRUE(NumberUtil::SafeStrToInt16("-012345", &value));
  EXPECT_EQ(-12345, value);
  value = 0x4321;
  EXPECT_TRUE(NumberUtil::SafeStrToInt16("-32768", &value));
  EXPECT_EQ(std::numeric_limits<int16_t>::min(), value);
  value = 0x4321;
  EXPECT_TRUE(NumberUtil::SafeStrToInt16("32767", &value));
  EXPECT_EQ(std::numeric_limits<int16_t>::max(), value);
  value = 0x4321;
  EXPECT_TRUE(NumberUtil::SafeStrToInt16(" 1", &value));
  EXPECT_EQ(1, value);
  value = 0x4321;
  EXPECT_TRUE(NumberUtil::SafeStrToInt16("2 ", &value));
  EXPECT_EQ(2, value);

  EXPECT_FALSE(NumberUtil::SafeStrToInt16("0x1234", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt16("-32769", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt16("32768", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt16("18446744073709551616", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt16("3e", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt16("0.", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt16(".0", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt16("", &value));

  // Test for absl::string_view input.
  const char *kString = "123 abc 789";
  EXPECT_TRUE(
      NumberUtil::SafeStrToInt16(absl::string_view(kString, 3), &value));
  EXPECT_EQ(123, value);
  EXPECT_FALSE(
      NumberUtil::SafeStrToInt16(absl::string_view(kString + 4, 3), &value));
  EXPECT_TRUE(
      NumberUtil::SafeStrToInt16(absl::string_view(kString + 8, 3), &value));
  EXPECT_EQ(789, value);
  EXPECT_TRUE(
      NumberUtil::SafeStrToInt16(absl::string_view(kString + 7, 4), &value));
  EXPECT_EQ(789, value);
}

TEST(NumberUtilTest, SafeStrToInt32) {
  int32_t value = 0xDEADBEEF;

  EXPECT_TRUE(NumberUtil::SafeStrToInt32("0", &value));
  EXPECT_EQ(0, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt32("+0", &value));
  EXPECT_EQ(0, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt32("-0", &value));
  EXPECT_EQ(0, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt32(" \t\r\n\v\f0 \t\r\n\v\f", &value));
  EXPECT_EQ(0, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt32(" \t\r\n\v\f-0 \t\r\n\v\f", &value));
  EXPECT_EQ(0, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt32("012345678", &value));
  EXPECT_EQ(12345678, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt32("-012345678", &value));
  EXPECT_EQ(-12345678, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt32("-2147483648", &value));
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt32("2147483647", &value));
  EXPECT_EQ(std::numeric_limits<int32_t>::max(), value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt32(" 1", &value));
  EXPECT_EQ(1, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt32("2 ", &value));
  EXPECT_EQ(2, value);

  EXPECT_FALSE(NumberUtil::SafeStrToInt32("0x1234", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt32("-2147483649", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt32("2147483648", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt32("18446744073709551616", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt32("3e", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt32("0.", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt32(".0", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt32("", &value));

  // Test for absl::string_view input.
  const char *kString = "123 abc 789";
  EXPECT_TRUE(
      NumberUtil::SafeStrToInt32(absl::string_view(kString, 3), &value));
  EXPECT_EQ(123, value);
  EXPECT_FALSE(
      NumberUtil::SafeStrToInt32(absl::string_view(kString + 4, 3), &value));
  EXPECT_TRUE(
      NumberUtil::SafeStrToInt32(absl::string_view(kString + 8, 3), &value));
  EXPECT_EQ(789, value);
  EXPECT_TRUE(
      NumberUtil::SafeStrToInt32(absl::string_view(kString + 7, 4), &value));
  EXPECT_EQ(789, value);
}

TEST(NumberUtilTest, SafeStrToInt64) {
  int64_t value = 0xDEADBEEF;

  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt64("0", &value));
  EXPECT_EQ(0, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt64("+0", &value));
  EXPECT_EQ(0, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt64("-0", &value));
  EXPECT_EQ(0, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt64(" \t\r\n\v\f0 \t\r\n\v\f", &value));
  EXPECT_EQ(0, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt64(" \t\r\n\v\f-0 \t\r\n\v\f", &value));
  EXPECT_EQ(0, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt64("012345678", &value));
  EXPECT_EQ(12345678, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt64("-012345678", &value));
  EXPECT_EQ(-12345678, value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt64("-9223372036854775808", &value));
  EXPECT_EQ(std::numeric_limits<int64_t>::min(), value);
  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToInt64("9223372036854775807", &value));
  EXPECT_EQ(std::numeric_limits<int64_t>::max(), value);

  EXPECT_FALSE(NumberUtil::SafeStrToInt64("-9223372036854775809",  // overflow
                                          &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt64("9223372036854775808",  // overflow
                                          &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt64("0x1234", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt64("3e", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt64("0.", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt64(".0", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToInt64("", &value));

  // Test for absl::string_view input.
  const char *kString = "123 abc 789";
  EXPECT_TRUE(
      NumberUtil::SafeStrToInt64(absl::string_view(kString, 3), &value));
  EXPECT_EQ(123, value);
  EXPECT_FALSE(
      NumberUtil::SafeStrToInt64(absl::string_view(kString + 4, 3), &value));
  EXPECT_TRUE(
      NumberUtil::SafeStrToInt64(absl::string_view(kString + 8, 3), &value));
  EXPECT_EQ(789, value);
}

TEST(NumberUtilTest, SafeStrToUInt16) {
  uint16_t value = 0xBEEF;

  EXPECT_TRUE(NumberUtil::SafeStrToUInt16("0", &value));
  EXPECT_EQ(0, value);

  value = 0xBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToUInt16(" \t\r\n\v\f0 \t\r\n\v\f", &value));
  EXPECT_EQ(0, value);

  value = 0xBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToUInt16("012345", &value));
  EXPECT_EQ(12345, value);

  value = 0xBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToUInt16("65535", &value));
  EXPECT_EQ(65535u, value);  // max of 16-bit unsigned integer

  value = 0xBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToUInt16(" 1", &value));
  EXPECT_EQ(1, value);

  value = 0xBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToUInt16("2 ", &value));
  EXPECT_EQ(2, value);

  EXPECT_FALSE(NumberUtil::SafeStrToUInt16("-0", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt16("0x1234", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt16("65536", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt16("18446744073709551616", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt16("3e", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt16("0.", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt16(".0", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt16("", &value));

  // Test for absl::string_view input.
  const char *kString = "123 abc 789";
  EXPECT_TRUE(
      NumberUtil::SafeStrToUInt16(absl::string_view(kString, 3), &value));
  EXPECT_EQ(123, value);
  EXPECT_FALSE(
      NumberUtil::SafeStrToUInt16(absl::string_view(kString + 4, 3), &value));
  EXPECT_TRUE(
      NumberUtil::SafeStrToUInt16(absl::string_view(kString + 8, 3), &value));
  EXPECT_EQ(789, value);
  EXPECT_TRUE(
      NumberUtil::SafeStrToUInt16(absl::string_view(kString + 7, 4), &value));
  EXPECT_EQ(789, value);
}

TEST(NumberUtilTest, SafeStrToUInt32) {
  uint32_t value = 0xDEADBEEF;

  EXPECT_TRUE(NumberUtil::SafeStrToUInt32("0", &value));
  EXPECT_EQ(0, value);

  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToUInt32(" \t\r\n\v\f0 \t\r\n\v\f", &value));
  EXPECT_EQ(0, value);

  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToUInt32("012345678", &value));
  EXPECT_EQ(12345678, value);

  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToUInt32("4294967295", &value));
  EXPECT_EQ(4294967295u, value);  // max of 32-bit unsigned integer

  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToUInt32(" 1", &value));
  EXPECT_EQ(1, value);

  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeStrToUInt32("2 ", &value));
  EXPECT_EQ(2, value);

  EXPECT_FALSE(NumberUtil::SafeStrToUInt32("-0", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt32("0x1234", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt32("4294967296", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt32("18446744073709551616", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt32("3e", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt32("0.", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt32(".0", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt32("", &value));

  // Test for absl::string_view input.
  const char *kString = "123 abc 789";
  EXPECT_TRUE(
      NumberUtil::SafeStrToUInt32(absl::string_view(kString, 3), &value));
  EXPECT_EQ(123, value);
  EXPECT_FALSE(
      NumberUtil::SafeStrToUInt32(absl::string_view(kString + 4, 3), &value));
  EXPECT_TRUE(
      NumberUtil::SafeStrToUInt32(absl::string_view(kString + 8, 3), &value));
  EXPECT_EQ(789, value);
  EXPECT_TRUE(
      NumberUtil::SafeStrToUInt32(absl::string_view(kString + 7, 4), &value));
  EXPECT_EQ(789, value);
}

TEST(NumberUtilTest, SafeHexStrToUInt32) {
  uint32_t value = 0xDEADBEEF;

  EXPECT_TRUE(NumberUtil::SafeHexStrToUInt32("0", &value));
  EXPECT_EQ(0, value);

  value = 0xDEADBEEF;
  EXPECT_TRUE(
      NumberUtil::SafeHexStrToUInt32(" \t\r\n\v\f0 \t\r\n\v\f", &value));
  EXPECT_EQ(0, value);

  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeHexStrToUInt32("0ABCDE", &value));
  EXPECT_EQ(0xABCDE, value);

  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeHexStrToUInt32("0abcde", &value));
  EXPECT_EQ(0xABCDE, value);

  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeHexStrToUInt32("FFFFFFFF", &value));
  EXPECT_EQ(0xFFFFFFFF, value);  // max of 32-bit unsigned integer

  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeHexStrToUInt32("ffffffff", &value));
  EXPECT_EQ(0xFFFFFFFF, value);  // max of 32-bit unsigned integer

  EXPECT_FALSE(NumberUtil::SafeHexStrToUInt32("-0", &value));
  EXPECT_FALSE(
      NumberUtil::SafeHexStrToUInt32("100000000", &value));  // overflow
  EXPECT_FALSE(NumberUtil::SafeHexStrToUInt32("GHIJK", &value));
  EXPECT_FALSE(NumberUtil::SafeHexStrToUInt32("0.", &value));
  EXPECT_FALSE(NumberUtil::SafeHexStrToUInt32(".0", &value));
  EXPECT_FALSE(NumberUtil::SafeHexStrToUInt32("", &value));

  // Test for absl::string_view input.
  const char *kString = "123 abc 5x";
  EXPECT_TRUE(
      NumberUtil::SafeHexStrToUInt32(absl::string_view(kString, 3), &value));
  EXPECT_EQ(291, value);
  EXPECT_TRUE(NumberUtil::SafeHexStrToUInt32(absl::string_view(kString + 4, 3),
                                             &value));
  EXPECT_EQ(2748, value);
  EXPECT_FALSE(NumberUtil::SafeHexStrToUInt32(absl::string_view(kString + 8, 2),
                                              &value));
}

TEST(NumberUtilTest, SafeOctStrToUInt32) {
  uint32_t value = 0xDEADBEEF;

  EXPECT_TRUE(NumberUtil::SafeOctStrToUInt32("0", &value));
  EXPECT_EQ(0, value);

  value = 0xDEADBEEF;
  EXPECT_TRUE(
      NumberUtil::SafeOctStrToUInt32(" \t\r\n\v\f0 \t\r\n\v\f", &value));
  EXPECT_EQ(0, value);

  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeOctStrToUInt32("012345", &value));
  EXPECT_EQ(012345, value);

  value = 0xDEADBEEF;
  EXPECT_TRUE(NumberUtil::SafeOctStrToUInt32("37777777777", &value));
  EXPECT_EQ(0xFFFFFFFF, value);  // max of 32-bit unsigned integer

  EXPECT_FALSE(NumberUtil::SafeOctStrToUInt32("-0", &value));
  EXPECT_FALSE(
      NumberUtil::SafeOctStrToUInt32("40000000000", &value));  // overflow
  EXPECT_FALSE(NumberUtil::SafeOctStrToUInt32("9AB", &value));
  EXPECT_FALSE(NumberUtil::SafeOctStrToUInt32("0.", &value));
  EXPECT_FALSE(NumberUtil::SafeOctStrToUInt32(".0", &value));
  EXPECT_FALSE(NumberUtil::SafeOctStrToUInt32("", &value));

  // Test for absl::string_view input.
  const char *kString = "123 456 789";
  EXPECT_TRUE(
      NumberUtil::SafeOctStrToUInt32(absl::string_view(kString, 3), &value));
  EXPECT_EQ(83, value);
  EXPECT_TRUE(NumberUtil::SafeOctStrToUInt32(absl::string_view(kString + 4, 3),
                                             &value));
  EXPECT_EQ(302, value);
  EXPECT_FALSE(NumberUtil::SafeOctStrToUInt32(absl::string_view(kString + 8, 3),
                                              &value));
}

TEST(NumberUtilTest, SafeStrToUInt64) {
  uint64_t value = 0xDEADBEEF;

  EXPECT_TRUE(NumberUtil::SafeStrToUInt64("0", &value));
  EXPECT_EQ(0, value);
  EXPECT_TRUE(NumberUtil::SafeStrToUInt64(" \t\r\n\v\f0 \t\r\n\v\f", &value));
  EXPECT_EQ(0, value);
  EXPECT_TRUE(NumberUtil::SafeStrToUInt64("012345678", &value));
  EXPECT_EQ(12345678, value);
  EXPECT_TRUE(NumberUtil::SafeStrToUInt64("18446744073709551615", &value));
  EXPECT_EQ(uint64_t{18446744073709551615u},
            value);  // max of 64-bit unsigned integer

  EXPECT_FALSE(NumberUtil::SafeStrToUInt64("-0", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt64("18446744073709551616",  // overflow
                                           &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt64("0x1234", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt64("3e", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt64("0.", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt64(".0", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToUInt64("", &value));

  // Test for absl::string_view input.
  const char *kString = "123 abc 789";
  EXPECT_TRUE(
      NumberUtil::SafeStrToUInt64(absl::string_view(kString, 3), &value));
  EXPECT_EQ(123, value);
  EXPECT_FALSE(
      NumberUtil::SafeStrToUInt64(absl::string_view(kString + 4, 3), &value));
  EXPECT_TRUE(
      NumberUtil::SafeStrToUInt64(absl::string_view(kString + 8, 3), &value));
  EXPECT_EQ(789, value);
}

TEST(NumberUtilTest, SafeStrToDouble) {
  double value = 1.0;

  EXPECT_TRUE(NumberUtil::SafeStrToDouble("0", &value));
  EXPECT_EQ(0.0, value);
  EXPECT_TRUE(NumberUtil::SafeStrToDouble(" \t\r\n\v\f0 \t\r\n\v\f", &value));
  EXPECT_EQ(0.0, value);
  EXPECT_TRUE(NumberUtil::SafeStrToDouble("-0", &value));
  EXPECT_EQ(0.0, value);
  EXPECT_TRUE(NumberUtil::SafeStrToDouble("1.0e1", &value));
  EXPECT_EQ(10.0, value);
  EXPECT_TRUE(NumberUtil::SafeStrToDouble("-5.0e-1", &value));
  EXPECT_EQ(-0.5, value);
  EXPECT_TRUE(NumberUtil::SafeStrToDouble(".0", &value));
  EXPECT_EQ(0.0, value);
  EXPECT_TRUE(NumberUtil::SafeStrToDouble("0.", &value));
  EXPECT_EQ(0.0, value);
  EXPECT_TRUE(NumberUtil::SafeStrToDouble("0.0", &value));
  EXPECT_EQ(0.0, value);
  // Approximate representation of max of double.  The value checking is done by
  // EXPECT_DOUBLE_EQ as the result might be very slightly different on some
  // platforms.
  EXPECT_TRUE(NumberUtil::SafeStrToDouble("1.7976931348623158e308", &value));
  EXPECT_DOUBLE_EQ(1.7976931348623158e308, value);
  EXPECT_TRUE(NumberUtil::SafeStrToDouble("-1.7976931348623158e308", &value));
  EXPECT_DOUBLE_EQ(-1.7976931348623158e308, value);

  // It seems that the Android libc doesn't accept hex format, so disable it.
#ifndef OS_ANDROID
  EXPECT_TRUE(NumberUtil::SafeStrToDouble("0x1234", &value));
  EXPECT_EQ(static_cast<double>(0x1234), value);
#endif  // OS_ANDROID

  EXPECT_FALSE(NumberUtil::SafeStrToDouble("1.0e309", &value));   // overflow
  EXPECT_FALSE(NumberUtil::SafeStrToDouble("-1.0e309", &value));  // underflow
  EXPECT_FALSE(NumberUtil::SafeStrToDouble("NaN", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToDouble("3e", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToDouble(".", &value));
  EXPECT_FALSE(NumberUtil::SafeStrToDouble("", &value));

  // Test for absl::string_view input.
  const char *kString = "0.01 3.1415 double";
  EXPECT_TRUE(
      NumberUtil::SafeStrToDouble(absl::string_view(kString, 4), &value));
  EXPECT_EQ(0.01, value);
  EXPECT_TRUE(
      NumberUtil::SafeStrToDouble(absl::string_view(kString + 5, 6), &value));
  EXPECT_EQ(3.1415, value);
  EXPECT_FALSE(
      NumberUtil::SafeStrToDouble(absl::string_view(kString + 12, 6), &value));
}

TEST(NumberUtilTest, IsArabicNumber) {
  EXPECT_FALSE(NumberUtil::IsArabicNumber(""));

  EXPECT_TRUE(NumberUtil::IsArabicNumber("0"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("1"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("2"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("3"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("4"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("5"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("6"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("7"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("8"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("9"));

  EXPECT_TRUE(NumberUtil::IsArabicNumber("???"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("???"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("???"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("???"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("???"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("???"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("???"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("???"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("???"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("???"));

  EXPECT_TRUE(NumberUtil::IsArabicNumber("0123456789"));
  EXPECT_TRUE(NumberUtil::IsArabicNumber("01234567890123456789"));

  EXPECT_TRUE(NumberUtil::IsArabicNumber("??????"));

  EXPECT_FALSE(NumberUtil::IsArabicNumber("abc"));
  EXPECT_FALSE(NumberUtil::IsArabicNumber("???"));
  EXPECT_FALSE(NumberUtil::IsArabicNumber("???"));

  EXPECT_FALSE(NumberUtil::IsArabicNumber("????????????"));
}

TEST(NumberUtilTest, IsDecimalInteger) {
  EXPECT_FALSE(NumberUtil::IsDecimalInteger(""));

  EXPECT_TRUE(NumberUtil::IsDecimalInteger("0"));
  EXPECT_TRUE(NumberUtil::IsDecimalInteger("1"));
  EXPECT_TRUE(NumberUtil::IsDecimalInteger("2"));
  EXPECT_TRUE(NumberUtil::IsDecimalInteger("3"));
  EXPECT_TRUE(NumberUtil::IsDecimalInteger("4"));
  EXPECT_TRUE(NumberUtil::IsDecimalInteger("5"));
  EXPECT_TRUE(NumberUtil::IsDecimalInteger("6"));
  EXPECT_TRUE(NumberUtil::IsDecimalInteger("7"));
  EXPECT_TRUE(NumberUtil::IsDecimalInteger("8"));
  EXPECT_TRUE(NumberUtil::IsDecimalInteger("9"));
  EXPECT_TRUE(NumberUtil::IsDecimalInteger("0123456789"));
  EXPECT_TRUE(NumberUtil::IsDecimalInteger("01234567890123456789"));

  EXPECT_FALSE(NumberUtil::IsDecimalInteger("???"));
  EXPECT_FALSE(NumberUtil::IsDecimalInteger("???"));
  EXPECT_FALSE(NumberUtil::IsDecimalInteger("???"));
  EXPECT_FALSE(NumberUtil::IsDecimalInteger("???"));
  EXPECT_FALSE(NumberUtil::IsDecimalInteger("???"));
  EXPECT_FALSE(NumberUtil::IsDecimalInteger("???"));
  EXPECT_FALSE(NumberUtil::IsDecimalInteger("???"));
  EXPECT_FALSE(NumberUtil::IsDecimalInteger("???"));
  EXPECT_FALSE(NumberUtil::IsDecimalInteger("???"));
  EXPECT_FALSE(NumberUtil::IsDecimalInteger("???"));

  EXPECT_FALSE(NumberUtil::IsDecimalInteger("??????"));
  EXPECT_FALSE(NumberUtil::IsDecimalInteger("???"));
  EXPECT_FALSE(NumberUtil::IsDecimalInteger("???"));
  EXPECT_FALSE(NumberUtil::IsDecimalInteger("abc"));
  EXPECT_FALSE(NumberUtil::IsDecimalInteger("????????????"));
}

TEST(NumberUtilTest, KanjiNumberToArabicNumber) {
  const char *inputs[] = {"???", "???", "???", "???", "???", "???", "???"};
  const char *expects[] = {"10",
                           "100",
                           "1000",
                           "10000",
                           "100000000",
                           "1000000000000",
                           "10000000000000000"};

  for (size_t i = 0; i < std::size(inputs); ++i) {
    std::string arabic;
    NumberUtil::KanjiNumberToArabicNumber(inputs[i], &arabic);
    EXPECT_EQ(expects[i], arabic);
  }
}

TEST(NumberUtilTest, NormalizeNumbers) {
  // An element has input, expected Kanji output, and expected Arabic output.
  const char *success_data[][3] = {
      {"???", "???", "1"},
      {"???", "???", "9"},
      {"???", "???", "10"},
      {"??????", "??????", "15"},
      {"??????", "??????", "20"},
      {"?????????", "?????????", "35"},
      {"???", "???", "100"},
      {"??????", "??????", "200"},
      {"?????????", "?????????", "210"},
      {"????????????", "????????????", "250"},
      {"???????????????", "???????????????", "777"},
      {"???", "???", "1000"},
      {"??????", "??????", "1000"},
      {"??????", "??????", "8000"},
      {"?????????????????????", "?????????????????????", "8739"},
      {"???????????????", "???????????????", "10025"},
      // 2^64 - 1
      {"????????????????????????????????????????????????????????????????????????????????????????????????",
       "????????????????????????????????????????????????????????????????????????????????????????????????",
       "18446744073709551615"},
      {"?????????", "?????????", "10000000100"},
      {"?????????", "?????????", "10000000000000000000"},

      // Old Kanji numbers
      {"???", "???", "0"},
      {"???", "???", "10"},
      {"??????", "??????", "14"},
      {"???", "???", "20"},
      {"?????????", "?????????", "200020"},
      {"?????????", "?????????", "23"},
      {"????????????", "????????????", "23"},

      // Array of Kanji number digits
      {"0", "???", "0"},
      {"00", "??????", "0"},
      {"?????????", "?????????", "235"},
      {"?????????", "?????????", "12"},
      {"????????????", "????????????", "2011"},

      // Combinations of several types
      {"??????????????????", "??????????????????", "2350043"},
      {"??????????????????", "??????????????????", "2350001"},
      {"2???5", "?????????", "25"},
      {"2????????????", "???????????????", "2043"},
      {"??????", "??????", "90"},
  };

  for (size_t i = 0; i < std::size(success_data); ++i) {
    std::string arabic_output = "dummy_text_arabic";
    std::string kanji_output = "dummy_text_kanji";
    EXPECT_TRUE(NumberUtil::NormalizeNumbers(success_data[i][0], true,
                                             &kanji_output, &arabic_output));
    EXPECT_EQ(success_data[i][1], kanji_output);
    EXPECT_EQ(success_data[i][2], arabic_output);
  }

  // An element has input, expected Kanji output, and expected Arabic output.
  const char *success_notrim_data[][3] = {
      {"?????????", "?????????", "012"},
      {"???00", "?????????", "000"},
      {"????????????", "????????????", "0012"},
      {"???????????????", "???????????????", "00012"},
      {"0", "???", "0"},
      {"00", "??????", "00"},
  };

  for (size_t i = 0; i < std::size(success_notrim_data); ++i) {
    std::string arabic_output = "dummy_text_arabic";
    std::string kanji_output = "dummy_text_kanji";
    EXPECT_TRUE(NumberUtil::NormalizeNumbers(success_notrim_data[i][0], false,
                                             &kanji_output, &arabic_output));
    EXPECT_EQ(success_notrim_data[i][1], kanji_output);
    EXPECT_EQ(success_notrim_data[i][2], arabic_output);
  }

  // Test data expected to fail
  const char *fail_data[] = {
      // 2^64
      "????????????????????????????????????????????????????????????????????????????????????????????????",
      "?????????",
      "????????????",
      "??????",
      "??????",
      "????????????????????????",  // lack of number before "???"
      "????????????",          // large base, "???", after small one, "???"
      "????????????",          // same base appears twice
      "????????????",          // same base appears twice
      "????????????",          // same base appears twice
      "?????????",            // relatively large base "???" after "???"
      "??????????????????????????????",
      "???????????????",
  };

  for (size_t i = 0; i < std::size(fail_data); ++i) {
    std::string arabic_output, kanji_output;
    EXPECT_FALSE(NumberUtil::NormalizeNumbers(fail_data[i], true, &kanji_output,
                                              &arabic_output));
  }
}

TEST(NumberUtilTest, NormalizeNumbersWithSuffix) {
  {
    // Checks that kanji_output and arabic_output is cleared.
    const std::string input = "??????";
    std::string arabic_output = "dummy_text_arabic";
    std::string kanji_output = "dummy_text_kanji";
    std::string suffix = "dummy_text_suffix";
    EXPECT_TRUE(NumberUtil::NormalizeNumbersWithSuffix(
        input, true, &kanji_output, &arabic_output, &suffix));
    EXPECT_EQ("???", kanji_output);
    EXPECT_EQ("1", arabic_output);
    EXPECT_EQ("???", suffix);
  }

  {
    const std::string input = "??????????????????";
    std::string arabic_output, kanji_output, suffix;
    EXPECT_TRUE(NumberUtil::NormalizeNumbersWithSuffix(
        input, true, &kanji_output, &arabic_output, &suffix));
    EXPECT_EQ("???????????????", kanji_output);
    EXPECT_EQ("10025", arabic_output);
    EXPECT_EQ("???", suffix);
  }

  {
    const std::string input = "????????????????????????";
    std::string arabic_output, kanji_output, suffix;
    EXPECT_TRUE(NumberUtil::NormalizeNumbersWithSuffix(
        input, true, &kanji_output, &arabic_output, &suffix));
    EXPECT_EQ("??????????????????", kanji_output);
    EXPECT_EQ("2350001", arabic_output);
    EXPECT_EQ("??????", suffix);
  }

  {
    const std::string input = "?????????";
    std::string arabic_output, kanji_output, suffix;
    EXPECT_FALSE(NumberUtil::NormalizeNumbersWithSuffix(
        input, true, &kanji_output, &arabic_output, &suffix));
  }

  {
    const std::string input = "????????????";
    std::string arabic_output, kanji_output, suffix;
    EXPECT_FALSE(NumberUtil::NormalizeNumbersWithSuffix(
        input, true, &kanji_output, &arabic_output, &suffix));
  }

  // Tests for numbers less than 10.
  {
    const std::string input = "????????????";
    std::string arabic_output, kanji_output, suffix;
    EXPECT_TRUE(NumberUtil::NormalizeNumbersWithSuffix(
        input, true, &kanji_output, &arabic_output, &suffix));
    EXPECT_EQ("???", kanji_output);
    EXPECT_EQ("0", arabic_output);
    EXPECT_EQ("?????????", suffix);
  }

  {
    const std::string input = "????????????";
    std::string arabic_output, kanji_output, suffix;
    EXPECT_TRUE(NumberUtil::NormalizeNumbersWithSuffix(
        input, true, &kanji_output, &arabic_output, &suffix));
    EXPECT_EQ("??????", kanji_output);
    EXPECT_EQ("90", arabic_output);
    EXPECT_EQ("??????", suffix);
  }

  {
    const std::string input = "??????$";
    std::string arabic_output, kanji_output, suffix;
    EXPECT_TRUE(NumberUtil::NormalizeNumbersWithSuffix(
        input, true, &kanji_output, &arabic_output, &suffix));
    EXPECT_EQ("??????", kanji_output);
    EXPECT_EQ("35", arabic_output);
    EXPECT_EQ("$", suffix);
  }

  {
    const std::string input = "???????????????";  // same base appears twice
    std::string arabic_output, kanji_output, suffix;
    EXPECT_FALSE(NumberUtil::NormalizeNumbersWithSuffix(
        input, true, &kanji_output, &arabic_output, &suffix));
  }
}

TEST(NumberUtilTest, ArabicToWideArabicTest) {
  std::string arabic;
  std::vector<NumberUtil::NumberString> output;

  arabic = "12345";
  output.clear();
  EXPECT_TRUE(NumberUtil::ArabicToWideArabic(arabic, &output));
  ASSERT_EQ(output.size(), 2);
  EXPECT_EQ("???????????????", output[0].value);
  EXPECT_EQ(NumberUtil::NumberString::NUMBER_KANJI_ARABIC, output[0].style);
  EXPECT_EQ("???????????????", output[1].value);
  EXPECT_EQ(NumberUtil::NumberString::DEFAULT_STYLE, output[1].style);

  arabic = "00123";
  output.clear();
  EXPECT_TRUE(NumberUtil::ArabicToWideArabic(arabic, &output));
  ASSERT_EQ(output.size(), 2);
  EXPECT_EQ("???????????????", output[0].value);
  EXPECT_EQ(NumberUtil::NumberString::NUMBER_KANJI_ARABIC, output[0].style);
  EXPECT_EQ("???????????????", output[1].value);
  EXPECT_EQ(NumberUtil::NumberString::DEFAULT_STYLE, output[1].style);

  arabic = "abcde";
  output.clear();
  EXPECT_FALSE(NumberUtil::ArabicToWideArabic(arabic, &output));
  EXPECT_EQ(output.size(), 0);

  arabic = "012abc345";
  output.clear();
  EXPECT_FALSE(NumberUtil::ArabicToWideArabic(arabic, &output));
  EXPECT_EQ(output.size(), 0);

  arabic = "0.001";
  output.clear();
  EXPECT_FALSE(NumberUtil::ArabicToWideArabic(arabic, &output));
  EXPECT_EQ(output.size(), 0);

  arabic = "-100";
  output.clear();
  EXPECT_FALSE(NumberUtil::ArabicToWideArabic(arabic, &output));
  EXPECT_EQ(output.size(), 0);

  arabic = "18446744073709551616";  // UINT_MAX + 1
  EXPECT_TRUE(NumberUtil::ArabicToWideArabic(arabic, &output));
  ASSERT_EQ(2, output.size());
  EXPECT_EQ("????????????????????????????????????????????????????????????", output[0].value);
  EXPECT_EQ("????????????????????????????????????????????????????????????", output[1].value);
  EXPECT_EQ(NumberUtil::NumberString::NUMBER_KANJI_ARABIC, output[0].style);
  EXPECT_EQ(NumberUtil::NumberString::DEFAULT_STYLE, output[1].style);
}

namespace {
constexpr int kMaxCandsInArabicToKanjiTest = 4;
struct ArabicToKanjiTestData {
  const char *input;
  const int expect_num;
  const char *expect_value[kMaxCandsInArabicToKanjiTest];
  const NumberUtil::NumberString::Style
      expect_style[kMaxCandsInArabicToKanjiTest];
};
}  // namespace

// ArabicToKanji TEST
TEST(NumberUtilTest, ArabicToKanjiTest) {
  const NumberUtil::NumberString::Style kOldKanji =
      NumberUtil::NumberString::NUMBER_OLD_KANJI;
  const NumberUtil::NumberString::Style kKanji =
      NumberUtil::NumberString::NUMBER_KANJI;
  const NumberUtil::NumberString::Style kHalfArabicKanji =
      NumberUtil::NumberString::NUMBER_ARABIC_AND_KANJI_HALFWIDTH;
  const NumberUtil::NumberString::Style kFullArabicKanji =
      NumberUtil::NumberString::NUMBER_ARABIC_AND_KANJI_FULLWIDTH;

  const ArabicToKanjiTestData kData[] = {
      {"0", 1, {"???"}, {kOldKanji}},
      {"00000", 1, {"???"}, {kOldKanji}},
      {"2", 2, {"???", "???"}, {kKanji, kOldKanji}},
      // "??????" is needed to avoid mistakes. Please refer http://b/6422355
      // for details.
      {"10", 3, {"???", "??????", "???"}, {kKanji, kOldKanji, kOldKanji}},
      {"100", 2, {"???", "??????"}, {kKanji, kOldKanji}},
      {"1000", 3, {"???", "??????", "???"}, {kKanji, kOldKanji, kOldKanji}},
      {"20", 3, {"??????", "??????", "???"}, {kKanji, kOldKanji, kOldKanji}},
      {"11111",
       4,
       {"1???1111", "??????????????????", "??????????????????", "???????????????????????????"},
       {kHalfArabicKanji, kFullArabicKanji, kKanji, kOldKanji}},
      {"12345",
       4,
       {"1???2345", "??????????????????", "???????????????????????????", "???????????????????????????"},
       {kHalfArabicKanji, kFullArabicKanji, kKanji, kOldKanji}},
      {"100002345",
       4,
       {"1???2345", "??????????????????", "???????????????????????????", "???????????????????????????"},
       {kHalfArabicKanji, kFullArabicKanji, kKanji, kOldKanji}},
      {"18446744073709551615",
       4,
       {"1844???6744???737???955???1615",
        "??????????????????????????????????????????????????????????????????",
        "????????????????????????????????????????????????????????????????????????????????????????????????",
        "??????????????????????????????????????????????????????????????????????????????????????????????????????"
        "???"},
       {kHalfArabicKanji, kFullArabicKanji, kKanji, kOldKanji}},
  };

  for (size_t i = 0; i < std::size(kData); ++i) {
    std::vector<NumberUtil::NumberString> output;
    ASSERT_LE(kData[i].expect_num, kMaxCandsInArabicToKanjiTest);
    EXPECT_TRUE(NumberUtil::ArabicToKanji(kData[i].input, &output));
    ASSERT_EQ(output.size(), kData[i].expect_num)
        << "on conversion of '" << kData[i].input << "'";
    for (int j = 0; j < kData[i].expect_num; ++j) {
      EXPECT_EQ(kData[i].expect_value[j], output[j].value)
          << "input : " << kData[i].input << "\nj : " << j;
      EXPECT_EQ(kData[i].expect_style[j], output[j].style)
          << "input : " << kData[i].input << "\nj : " << j;
    }
  }

  const char *kFailInputs[] = {"asf56789", "0.001", "-100",
                               "123456789012345678901"};
  for (size_t i = 0; i < std::size(kFailInputs); ++i) {
    std::vector<NumberUtil::NumberString> output;
    EXPECT_FALSE(NumberUtil::ArabicToKanji(kFailInputs[i], &output));
    ASSERT_EQ(output.size(), 0) << "input : " << kFailInputs[i];
  }
}

// ArabicToSeparatedArabic TEST
TEST(NumberUtilTest, ArabicToSeparatedArabicTest) {
  std::string arabic;
  std::vector<NumberUtil::NumberString> output;

  // Test data expected to succeed
  const char *kSuccess[][3] = {
      {"4", "4", "???"},
      {"123456789", "123,456,789", "?????????????????????????????????"},
      {"1234567.89", "1,234,567.89", "????????????????????????????????????"},
      // UINT64_MAX + 1
      {"18446744073709551616", "18,446,744,073,709,551,616", nullptr},
  };

  for (size_t i = 0; i < std::size(kSuccess); ++i) {
    arabic = kSuccess[i][0];
    output.clear();
    EXPECT_TRUE(NumberUtil::ArabicToSeparatedArabic(arabic, &output));
    ASSERT_EQ(output.size(), 2);
    EXPECT_EQ(kSuccess[i][1], output[0].value);
    EXPECT_EQ(NumberUtil::NumberString::NUMBER_SEPARATED_ARABIC_HALFWIDTH,
              output[0].style);
    if (kSuccess[i][2]) {
      EXPECT_EQ(kSuccess[i][2], output[1].value);
      EXPECT_EQ(NumberUtil::NumberString::NUMBER_SEPARATED_ARABIC_FULLWIDTH,
                output[1].style);
    }
  }

  // Test data expected to fail
  const char *kFail[] = {
      "0123456789",
      "asdf0123456789",
      "0.001",
      "-100",
  };

  for (size_t i = 0; i < std::size(kFail); ++i) {
    arabic = kFail[i];
    output.clear();
    EXPECT_FALSE(NumberUtil::ArabicToSeparatedArabic(arabic, &output));
    ASSERT_EQ(output.size(), 0);
  }
}

// ArabicToOtherForms
TEST(NumberUtilTest, ArabicToOtherFormsTest) {
  std::string arabic;
  std::vector<NumberUtil::NumberString> output;

  arabic = "5";
  output.clear();
  EXPECT_TRUE(NumberUtil::ArabicToOtherForms(arabic, &output));
  ASSERT_EQ(output.size(), 3);

  EXPECT_EQ("???", output[0].value);
  EXPECT_EQ(NumberUtil::NumberString::NUMBER_ROMAN_CAPITAL, output[0].style);

  EXPECT_EQ("???", output[1].value);
  EXPECT_EQ(NumberUtil::NumberString::NUMBER_ROMAN_SMALL, output[1].style);

  EXPECT_EQ("???", output[2].value);
  EXPECT_EQ(NumberUtil::NumberString::NUMBER_CIRCLED, output[2].style);

  arabic = "0123456789";
  output.clear();
  EXPECT_FALSE(NumberUtil::ArabicToOtherForms(arabic, &output));
  ASSERT_EQ(output.size(), 0);

  arabic = "asdf0123456789";
  output.clear();
  EXPECT_FALSE(NumberUtil::ArabicToOtherForms(arabic, &output));
  ASSERT_EQ(output.size(), 0);

  arabic = "0.001";
  output.clear();
  EXPECT_FALSE(NumberUtil::ArabicToOtherForms(arabic, &output));
  ASSERT_EQ(output.size(), 0);

  arabic = "-100";
  output.clear();
  EXPECT_FALSE(NumberUtil::ArabicToOtherForms(arabic, &output));
  ASSERT_EQ(output.size(), 0);

  arabic = "18446744073709551616";  // UINT64_MAX + 1
  output.clear();
  EXPECT_FALSE(NumberUtil::ArabicToOtherForms(arabic, &output));
}

// ArabicToOtherRadixes
TEST(NumberUtilTest, ArabicToOtherRadixesTest) {
  std::string arabic;
  std::vector<NumberUtil::NumberString> output;

  arabic = "1";
  output.clear();
  // "1" is "1" in any radixes.
  EXPECT_FALSE(NumberUtil::ArabicToOtherRadixes(arabic, &output));

  arabic = "2";
  output.clear();
  EXPECT_TRUE(NumberUtil::ArabicToOtherRadixes(arabic, &output));
  ASSERT_EQ(output.size(), 1);

  arabic = "8";
  output.clear();
  EXPECT_TRUE(NumberUtil::ArabicToOtherRadixes(arabic, &output));
  ASSERT_EQ(output.size(), 2);
  EXPECT_EQ("010", output[0].value);
  EXPECT_EQ(NumberUtil::NumberString::NUMBER_OCT, output[0].style);
  EXPECT_EQ("0b1000", output[1].value);
  EXPECT_EQ(NumberUtil::NumberString::NUMBER_BIN, output[1].style);

  arabic = "16";
  output.clear();
  EXPECT_TRUE(NumberUtil::ArabicToOtherRadixes(arabic, &output));
  ASSERT_EQ(output.size(), 3);
  EXPECT_EQ("0x10", output[0].value);
  EXPECT_EQ(NumberUtil::NumberString::NUMBER_HEX, output[0].style);
  EXPECT_EQ("020", output[1].value);
  EXPECT_EQ(NumberUtil::NumberString::NUMBER_OCT, output[1].style);
  EXPECT_EQ("0b10000", output[2].value);
  EXPECT_EQ(NumberUtil::NumberString::NUMBER_BIN, output[2].style);

  arabic = "asdf0123456789";
  output.clear();
  EXPECT_FALSE(NumberUtil::ArabicToOtherRadixes(arabic, &output));
  ASSERT_EQ(output.size(), 0);

  arabic = "0.001";
  output.clear();
  EXPECT_FALSE(NumberUtil::ArabicToOtherRadixes(arabic, &output));
  ASSERT_EQ(output.size(), 0);

  arabic = "-100";
  output.clear();
  EXPECT_FALSE(NumberUtil::ArabicToOtherRadixes(arabic, &output));
  ASSERT_EQ(output.size(), 0);

  arabic = "18446744073709551616";  // UINT64_MAX + 1
  output.clear();
  EXPECT_FALSE(NumberUtil::ArabicToOtherRadixes(arabic, &output));
}

}  // namespace
}  // namespace mozc
