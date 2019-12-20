// Copyright (C) 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "icing/tokenization/language-segmenter.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "icing/absl_ports/str_cat.h"
#include "icing/testing/common-matchers.h"
#include "icing/testing/i18n-test-utils.h"
#include "icing/testing/test-data.h"

namespace icing {
namespace lib {
namespace {
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;

class LanguageSegmenterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ICING_ASSERT_OK(
        // File generated via icu_data_file rule in //icing/BUILD.
        SetUpICUDataFile("icing/icu.dat"));
  }
};

TEST_F(LanguageSegmenterTest, BadModelPath) {
  EXPECT_THAT(LanguageSegmenter::Create("Bad Model Path"),
              StatusIs(libtextclassifier3::StatusCode::INVALID_ARGUMENT));
}

TEST_F(LanguageSegmenterTest, EmptyText) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  EXPECT_THAT(language_segmenter->GetAllTerms(""), IsOkAndHolds(IsEmpty()));
}

TEST_F(LanguageSegmenterTest, SimpleText) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  EXPECT_THAT(language_segmenter->GetAllTerms("Hello World"),
              IsOkAndHolds(ElementsAre("Hello", " ", "World")));
}

TEST_F(LanguageSegmenterTest, ASCII_Punctuation) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  // ASCII punctuation marks are kept
  EXPECT_THAT(
      language_segmenter->GetAllTerms("Hello, World!!!"),
      IsOkAndHolds(ElementsAre("Hello", ",", " ", "World", "!", "!", "!")));
  EXPECT_THAT(language_segmenter->GetAllTerms("Open-source project"),
              IsOkAndHolds(ElementsAre("Open", "-", "source", " ", "project")));
  EXPECT_THAT(language_segmenter->GetAllTerms("100%"),
              IsOkAndHolds(ElementsAre("100", "%")));
  EXPECT_THAT(language_segmenter->GetAllTerms("A&B"),
              IsOkAndHolds(ElementsAre("A", "&", "B")));
}

TEST_F(LanguageSegmenterTest, ASCII_SpecialCharacter) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  // ASCII special characters are kept
  EXPECT_THAT(language_segmenter->GetAllTerms("Pay $1000"),
              IsOkAndHolds(ElementsAre("Pay", " ", "$", "1000")));
  EXPECT_THAT(language_segmenter->GetAllTerms("A+B"),
              IsOkAndHolds(ElementsAre("A", "+", "B")));
  // 0x0009 is the unicode for tab (within ASCII range).
  std::string text_with_tab = absl_ports::StrCat(
      "Hello", UcharToString(0x0009), UcharToString(0x0009), "World");
  EXPECT_THAT(language_segmenter->GetAllTerms(text_with_tab),
              IsOkAndHolds(ElementsAre("Hello", UcharToString(0x0009),
                                       UcharToString(0x0009), "World")));
}

TEST_F(LanguageSegmenterTest, Non_ASCII_Non_Alphabetic) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  // Full-width (non-ASCII) punctuation marks and special characters are left
  // out.
  EXPECT_THAT(language_segmenter->GetAllTerms("。？·Hello！×"),
              IsOkAndHolds(ElementsAre("Hello")));
}

TEST_F(LanguageSegmenterTest, Acronym) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  EXPECT_THAT(language_segmenter->GetAllTerms("U.S. Bank"),
              IsOkAndHolds(ElementsAre("U.S", ".", " ", "Bank")));
  EXPECT_THAT(language_segmenter->GetAllTerms("I.B.M."),
              IsOkAndHolds(ElementsAre("I.B.M", ".")));
  EXPECT_THAT(language_segmenter->GetAllTerms("I,B,M"),
              IsOkAndHolds(ElementsAre("I", ",", "B", ",", "M")));
  EXPECT_THAT(language_segmenter->GetAllTerms("I B M"),
              IsOkAndHolds(ElementsAre("I", " ", "B", " ", "M")));
}

TEST_F(LanguageSegmenterTest, WordConnector) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  // According to unicode word break rules
  // WB6(https://unicode.org/reports/tr29/#WB6),
  // WB7(https://unicode.org/reports/tr29/#WB7), and a few others, some
  // punctuation characters are used as word connecters. That is, words don't
  // break before and after them. Here we just test some that we care about.

  // Word connecters
  EXPECT_THAT(language_segmenter->GetAllTerms("com.google.android"),
              IsOkAndHolds(ElementsAre("com.google.android")));
  EXPECT_THAT(language_segmenter->GetAllTerms("com:google:android"),
              IsOkAndHolds(ElementsAre("com:google:android")));
  EXPECT_THAT(language_segmenter->GetAllTerms("com'google'android"),
              IsOkAndHolds(ElementsAre("com'google'android")));
  EXPECT_THAT(language_segmenter->GetAllTerms("com_google_android"),
              IsOkAndHolds(ElementsAre("com_google_android")));

  // Word connecters can be mixed
  EXPECT_THAT(language_segmenter->GetAllTerms("com.google.android:icing"),
              IsOkAndHolds(ElementsAre("com.google.android:icing")));

  // Any heading and trailing characters are not connecters
  EXPECT_THAT(language_segmenter->GetAllTerms(".com.google.android."),
              IsOkAndHolds(ElementsAre(".", "com.google.android", ".")));

  // Not word connecters
  EXPECT_THAT(language_segmenter->GetAllTerms("com,google,android"),
              IsOkAndHolds(ElementsAre("com", ",", "google", ",", "android")));
  EXPECT_THAT(language_segmenter->GetAllTerms("com-google-android"),
              IsOkAndHolds(ElementsAre("com", "-", "google", "-", "android")));
  EXPECT_THAT(language_segmenter->GetAllTerms("com+google+android"),
              IsOkAndHolds(ElementsAre("com", "+", "google", "+", "android")));
  EXPECT_THAT(language_segmenter->GetAllTerms("com*google*android"),
              IsOkAndHolds(ElementsAre("com", "*", "google", "*", "android")));
  EXPECT_THAT(language_segmenter->GetAllTerms("com@google@android"),
              IsOkAndHolds(ElementsAre("com", "@", "google", "@", "android")));
  EXPECT_THAT(language_segmenter->GetAllTerms("com^google^android"),
              IsOkAndHolds(ElementsAre("com", "^", "google", "^", "android")));
  EXPECT_THAT(language_segmenter->GetAllTerms("com&google&android"),
              IsOkAndHolds(ElementsAre("com", "&", "google", "&", "android")));
  EXPECT_THAT(language_segmenter->GetAllTerms("com|google|android"),
              IsOkAndHolds(ElementsAre("com", "|", "google", "|", "android")));
  EXPECT_THAT(language_segmenter->GetAllTerms("com/google/android"),
              IsOkAndHolds(ElementsAre("com", "/", "google", "/", "android")));
  EXPECT_THAT(language_segmenter->GetAllTerms("com;google;android"),
              IsOkAndHolds(ElementsAre("com", ";", "google", ";", "android")));
  EXPECT_THAT(
      language_segmenter->GetAllTerms("com\"google\"android"),
      IsOkAndHolds(ElementsAre("com", "\"", "google", "\"", "android")));
}

TEST_F(LanguageSegmenterTest, Apostrophes) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  EXPECT_THAT(language_segmenter->GetAllTerms("It's ok."),
              IsOkAndHolds(ElementsAre("It's", " ", "ok", ".")));
  EXPECT_THAT(language_segmenter->GetAllTerms("He'll be back."),
              IsOkAndHolds(ElementsAre("He'll", " ", "be", " ", "back", ".")));
  EXPECT_THAT(language_segmenter->GetAllTerms("'Hello 'World."),
              IsOkAndHolds(ElementsAre("'", "Hello", " ", "'", "World", ".")));
  EXPECT_THAT(language_segmenter->GetAllTerms("The dogs' bone"),
              IsOkAndHolds(ElementsAre("The", " ", "dogs", "'", " ", "bone")));
  // 0x2019 is the single right quote, should be treated the same as "'"
  std::string token_with_quote =
      absl_ports::StrCat("He", UcharToString(0x2019), "ll");
  std::string text_with_quote =
      absl_ports::StrCat(token_with_quote, " be back.");
  EXPECT_THAT(
      language_segmenter->GetAllTerms(text_with_quote),
      IsOkAndHolds(ElementsAre(token_with_quote, " ", "be", " ", "back", ".")));
}

TEST_F(LanguageSegmenterTest, Parentheses) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));

  EXPECT_THAT(language_segmenter->GetAllTerms("(Hello)"),
              IsOkAndHolds(ElementsAre("(", "Hello", ")")));

  EXPECT_THAT(language_segmenter->GetAllTerms(")Hello("),
              IsOkAndHolds(ElementsAre(")", "Hello", "(")));
}

TEST_F(LanguageSegmenterTest, Quotes) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));

  EXPECT_THAT(language_segmenter->GetAllTerms("\"Hello\""),
              IsOkAndHolds(ElementsAre("\"", "Hello", "\"")));

  EXPECT_THAT(language_segmenter->GetAllTerms("'Hello'"),
              IsOkAndHolds(ElementsAre("'", "Hello", "'")));
}

TEST_F(LanguageSegmenterTest, Alphanumeric) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));

  // Alphanumeric terms are allowed
  EXPECT_THAT(language_segmenter->GetAllTerms("Se7en A4 3a"),
              IsOkAndHolds(ElementsAre("Se7en", " ", "A4", " ", "3a")));
}

TEST_F(LanguageSegmenterTest, Number) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));

  // Alphanumeric terms are allowed
  EXPECT_THAT(
      language_segmenter->GetAllTerms("3.141592653589793238462643383279"),
      IsOkAndHolds(ElementsAre("3.141592653589793238462643383279")));

  EXPECT_THAT(language_segmenter->GetAllTerms("3,456.789"),
              IsOkAndHolds(ElementsAre("3,456.789")));

  EXPECT_THAT(language_segmenter->GetAllTerms("-123"),
              IsOkAndHolds(ElementsAre("-", "123")));
}

TEST_F(LanguageSegmenterTest, ContinuousWhitespaces) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  // Multiple continuous whitespaces are treated as one.
  const int kNumSeparators = 256;
  const std::string text_with_spaces =
      absl_ports::StrCat("Hello", std::string(kNumSeparators, ' '), "World");
  EXPECT_THAT(language_segmenter->GetAllTerms(text_with_spaces),
              IsOkAndHolds(ElementsAre("Hello", " ", "World")));
}

TEST_F(LanguageSegmenterTest, CJKT) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  // CJKT (Chinese, Japanese, Khmer, Thai) are the 4 main languages that don't
  // have whitespaces as word delimiter.

  // Chinese
  EXPECT_THAT(language_segmenter->GetAllTerms("我每天走路去上班。"),
              IsOkAndHolds(ElementsAre("我", "每天", "走路", "去", "上班")));
  // Japanese
  EXPECT_THAT(language_segmenter->GetAllTerms("私は毎日仕事に歩いています。"),
              IsOkAndHolds(ElementsAre("私", "は", "毎日", "仕事", "に", "歩",
                                       "い", "てい", "ます")));
  // Khmer
  EXPECT_THAT(language_segmenter->GetAllTerms("ញុំដើរទៅធ្វើការរាល់ថ្ងៃ។"),
              IsOkAndHolds(ElementsAre("ញុំ", "ដើរទៅ", "ធ្វើការ", "រាល់ថ្ងៃ")));
  // Thai
  EXPECT_THAT(
      language_segmenter->GetAllTerms("ฉันเดินไปทำงานทุกวัน"),
      IsOkAndHolds(ElementsAre("ฉัน", "เดิน", "ไป", "ทำงาน", "ทุก", "วัน")));
}

TEST_F(LanguageSegmenterTest, LatinLettersWithAccents) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  EXPECT_THAT(language_segmenter->GetAllTerms("āăąḃḅḇčćç"),
              IsOkAndHolds(ElementsAre("āăąḃḅḇčćç")));
}

// TODO(samzheng): test cases for more languages (e.g. top 20 in the world)
TEST_F(LanguageSegmenterTest, WhitespaceSplitLanguages) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  // Turkish
  EXPECT_THAT(language_segmenter->GetAllTerms("merhaba dünya"),
              IsOkAndHolds(ElementsAre("merhaba", " ", "dünya")));
  // Korean
  EXPECT_THAT(
      language_segmenter->GetAllTerms("나는 매일 출근합니다."),
      IsOkAndHolds(ElementsAre("나는", " ", "매일", " ", "출근합니다", ".")));
}

// TODO(samzheng): more mixed languages test cases
TEST_F(LanguageSegmenterTest, MixedLanguages) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  EXPECT_THAT(language_segmenter->GetAllTerms("How are you你好吗お元気ですか"),
              IsOkAndHolds(ElementsAre("How", " ", "are", " ", "you", "你好",
                                       "吗", "お", "元気", "です", "か")));
}

TEST_F(LanguageSegmenterTest, NotCopyStrings) {
  ICING_ASSERT_OK_AND_ASSIGN(auto language_segmenter,
                             LanguageSegmenter::Create(GetLangIdModelPath()));
  // Validates that the input strings are not copied
  const std::string text = "Hello World";
  const char* word1_address = text.c_str();
  const char* word2_address = text.c_str() + 6;
  ICING_ASSERT_OK_AND_ASSIGN(std::vector<std::string_view> terms,
                             language_segmenter->GetAllTerms(text));
  ASSERT_THAT(terms, ElementsAre("Hello", " ", "World"));
  const char* word1_result_address = terms.at(0).data();
  const char* word2_result_address = terms.at(2).data();

  // The underlying char* should be the same
  EXPECT_THAT(word1_address, Eq(word1_result_address));
  EXPECT_THAT(word2_address, Eq(word2_result_address));
}

}  // namespace
}  // namespace lib
}  // namespace icing
