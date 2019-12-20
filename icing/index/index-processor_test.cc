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

#include "icing/index/index-processor.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "icing/absl_ports/str_cat.h"
#include "icing/document-builder.h"
#include "icing/file/filesystem.h"
#include "icing/index/hit/doc-hit-info.h"
#include "icing/index/index.h"
#include "icing/index/iterator/doc-hit-info-iterator.h"
#include "icing/legacy/index/icing-filesystem.h"
#include "icing/proto/document.pb.h"
#include "icing/proto/schema.pb.h"
#include "icing/proto/term.pb.h"
#include "icing/schema/schema-store.h"
#include "icing/schema/schema-util.h"
#include "icing/schema/section-manager.h"
#include "icing/schema/section.h"
#include "icing/store/document-id.h"
#include "icing/testing/common-matchers.h"
#include "icing/testing/test-data.h"
#include "icing/testing/tmp-directory.h"
#include "icing/tokenization/language-segmenter.h"
#include "icing/transform/normalizer.h"

namespace icing {
namespace lib {

namespace {

// type and property names of FakeType
constexpr std::string_view kFakeType = "FakeType";
constexpr std::string_view kExactProperty = "exact";
constexpr std::string_view kPrefixedProperty = "prefixed";
constexpr std::string_view kUnindexedProperty1 = "unindexed1";
constexpr std::string_view kUnindexedProperty2 = "unindexed2";
constexpr std::string_view kSubProperty = "submessage";
constexpr std::string_view kNestedProperty = "nested";
constexpr std::string_view kRepeatedProperty = "repeated";

constexpr DocumentId kDocumentId0 = 0;
constexpr DocumentId kDocumentId1 = 1;

constexpr SectionId kExactSectionId = 0;
constexpr SectionId kPrefixedSectionId = 1;
constexpr SectionId kRepeatedSectionId = 2;
constexpr SectionId kNestedSectionId = 3;

using Cardinality = PropertyConfigProto::Cardinality;
using DataType = PropertyConfigProto::DataType;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Test;

class IndexProcessorTest : public Test {
 protected:
  void SetUp() override {
    ICING_ASSERT_OK(
        // File generated via icu_data_file rule in //icing/BUILD.
        SetUpICUDataFile("icing/icu.dat"));

    index_dir_ = GetTestTempDir() + "/index_test/";
    Index::Options options(index_dir_, /*index_merge_size=*/1024 * 1024);
    ICING_ASSERT_OK_AND_ASSIGN(index_,
                               Index::Create(options, &icing_filesystem_));

    ICING_ASSERT_OK_AND_ASSIGN(lang_segmenter_,
                               LanguageSegmenter::Create(GetLangIdModelPath()));

    ICING_ASSERT_OK_AND_ASSIGN(
        normalizer_,
        Normalizer::Create(
            /*max_term_byte_size=*/std::numeric_limits<int32_t>::max()));

    ICING_ASSERT_OK_AND_ASSIGN(
        schema_store_, SchemaStore::Create(&filesystem_, GetTestTempDir()));
    SchemaProto schema;
    CreateFakeTypeConfig(schema.add_types());
    ICING_ASSERT_OK(schema_store_->SetSchema(schema));

    IndexProcessor::Options processor_options;
    processor_options.max_tokens_per_document = 1000;
    processor_options.token_limit_behavior =
        IndexProcessor::Options::TokenLimitBehavior::kReturnError;
    index_processor_ = std::make_unique<IndexProcessor>(
        schema_store_.get(), lang_segmenter_.get(), normalizer_.get(),
        index_.get(), processor_options);
  }

  void TearDown() override {
    filesystem_.DeleteDirectoryRecursively(index_dir_.c_str());
  }

  std::unique_ptr<IndexProcessor> index_processor_;
  std::unique_ptr<LanguageSegmenter> lang_segmenter_;
  std::unique_ptr<Normalizer> normalizer_;
  std::unique_ptr<Index> index_;
  std::unique_ptr<SchemaStore> schema_store_;

 private:
  static void AddProperty(std::string_view name, DataType::Code type,
                          Cardinality::Code cardinality,
                          TermMatchType::Code term_match_type,
                          SchemaTypeConfigProto* type_config) {
    auto* prop = type_config->add_properties();
    prop->set_property_name(std::string(name));
    prop->set_data_type(type);
    prop->set_cardinality(cardinality);
    prop->mutable_indexing_config()->set_term_match_type(term_match_type);
    prop->mutable_indexing_config()->set_tokenizer_type(
        IndexingConfig::TokenizerType::PLAIN);
  }

  static void CreateFakeTypeConfig(SchemaTypeConfigProto* type_config) {
    type_config->set_schema_type(std::string(kFakeType));

    AddProperty(std::string(kExactProperty), DataType::STRING,
                Cardinality::REQUIRED, TermMatchType::EXACT_ONLY, type_config);

    AddProperty(std::string(kPrefixedProperty), DataType::STRING,
                Cardinality::OPTIONAL, TermMatchType::PREFIX, type_config);

    // Don't set IndexingConfig
    auto* prop = type_config->add_properties();
    prop->set_property_name(std::string(kUnindexedProperty1));
    prop->set_data_type(DataType::STRING);
    prop->set_cardinality(Cardinality::OPTIONAL);

    AddProperty(std::string(kUnindexedProperty2), DataType::BYTES,
                Cardinality::OPTIONAL, TermMatchType::UNKNOWN, type_config);

    AddProperty(std::string(kRepeatedProperty), DataType::STRING,
                Cardinality::REPEATED, TermMatchType::PREFIX, type_config);

    AddProperty(kSubProperty, DataType::DOCUMENT, Cardinality::OPTIONAL,
                TermMatchType::UNKNOWN, type_config);

    std::string recipients_name =
        absl_ports::StrCat(kSubProperty, kPropertySeparator, kNestedProperty);
    AddProperty(recipients_name, DataType::STRING, Cardinality::OPTIONAL,
                TermMatchType::PREFIX, type_config);
  }

  Filesystem filesystem_;
  IcingFilesystem icing_filesystem_;
  std::string index_dir_;
};

std::vector<DocHitInfo> GetHits(std::unique_ptr<DocHitInfoIterator> iterator) {
  std::vector<DocHitInfo> infos;
  while (iterator->Advance().ok()) {
    infos.push_back(iterator->doc_hit_info());
  }
  return infos;
}

TEST_F(IndexProcessorTest, NoTermMatchTypeContent) {
  DocumentProto document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/1")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kUnindexedProperty1), "foo bar baz")
          .AddBytesProperty(std::string(kUnindexedProperty2),
                            "attachment bytes")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kInvalidDocumentId));
}

TEST_F(IndexProcessorTest, OneDoc) {
  DocumentProto document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/1")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty), "hello world")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));

  ICING_ASSERT_OK_AND_ASSIGN(std::unique_ptr<DocHitInfoIterator> itr,
                             index_->GetIterator("hello", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kExactSectionId})));

  ICING_ASSERT_OK_AND_ASSIGN(
      itr, index_->GetIterator("hello", 1U << kPrefixedSectionId,
                               TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)), IsEmpty());
}

TEST_F(IndexProcessorTest, MultipleDocs) {
  DocumentProto document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/1")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty), "hello world")
          .AddStringProperty(std::string(kPrefixedProperty), "good night moon!")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));

  document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/2")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty), "pitbull")
          .AddStringProperty(std::string(kPrefixedProperty), "mr. world wide")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId1), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId1));

  ICING_ASSERT_OK_AND_ASSIGN(std::unique_ptr<DocHitInfoIterator> itr,
                             index_->GetIterator("world", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(
      GetHits(std::move(itr)),
      ElementsAre(EqualsDocHitInfo(kDocumentId1,
                                   std::vector<SectionId>{kPrefixedSectionId}),
                  EqualsDocHitInfo(kDocumentId0,
                                   std::vector<SectionId>{kExactSectionId})));

  ICING_ASSERT_OK_AND_ASSIGN(
      itr, index_->GetIterator("world", 1U << kPrefixedSectionId,
                               TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId1, std::vector<SectionId>{kPrefixedSectionId})));
}

TEST_F(IndexProcessorTest, DocWithNestedProperty) {
  DocumentProto document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/1")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty), "hello world")
          .AddDocumentProperty(
              std::string(kSubProperty),
              DocumentBuilder()
                  .AddStringProperty(std::string(kNestedProperty),
                                     "rocky raccoon")
                  .Build())
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));

  ICING_ASSERT_OK_AND_ASSIGN(std::unique_ptr<DocHitInfoIterator> itr,
                             index_->GetIterator("rocky", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kNestedSectionId})));
}

TEST_F(IndexProcessorTest, DocWithRepeatedProperty) {
  DocumentProto document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/1")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty), "hello world")
          .AddStringProperty(std::string(kRepeatedProperty), "rocky",
                             "italian stallion")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));

  ICING_ASSERT_OK_AND_ASSIGN(std::unique_ptr<DocHitInfoIterator> itr,
                             index_->GetIterator("italian", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kRepeatedSectionId})));
}

TEST_F(IndexProcessorTest, TooManyTokensReturnError) {
  // Only allow the first four tokens ("hello", "world", "good", "night") to be
  // indexed.
  IndexProcessor::Options options;
  options.max_tokens_per_document = 4;
  options.token_limit_behavior =
      IndexProcessor::Options::TokenLimitBehavior::kReturnError;
  index_processor_ = std::make_unique<IndexProcessor>(
      schema_store_.get(), lang_segmenter_.get(), normalizer_.get(),
      index_.get(), options);
  DocumentProto document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/1")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty), "hello world")
          .AddStringProperty(std::string(kPrefixedProperty), "good night moon!")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0),
              StatusIs(libtextclassifier3::StatusCode::RESOURCE_EXHAUSTED));
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));

  // "night" should have been indexed.
  ICING_ASSERT_OK_AND_ASSIGN(std::unique_ptr<DocHitInfoIterator> itr,
                             index_->GetIterator("night", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kPrefixedSectionId})));

  // "moon" should not have been.
  ICING_ASSERT_OK_AND_ASSIGN(itr,
                             index_->GetIterator("moon", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)), IsEmpty());
}

TEST_F(IndexProcessorTest, TooManyTokensSuppressError) {
  // Only allow the first four tokens ("hello", "world", "good", "night") to be
  // indexed.
  IndexProcessor::Options options;
  options.max_tokens_per_document = 4;
  options.token_limit_behavior =
      IndexProcessor::Options::TokenLimitBehavior::kSuppressError;
  index_processor_ = std::make_unique<IndexProcessor>(
      schema_store_.get(), lang_segmenter_.get(), normalizer_.get(),
      index_.get(), options);
  DocumentProto document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/1")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty), "hello world")
          .AddStringProperty(std::string(kPrefixedProperty), "good night moon!")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));

  // "night" should have been indexed.
  ICING_ASSERT_OK_AND_ASSIGN(std::unique_ptr<DocHitInfoIterator> itr,
                             index_->GetIterator("night", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kPrefixedSectionId})));

  // "moon" should not have been.
  ICING_ASSERT_OK_AND_ASSIGN(itr,
                             index_->GetIterator("moon", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)), IsEmpty());
}

TEST_F(IndexProcessorTest, TooLongTokens) {
  // Only allow the tokens of length four, truncating "hello", "world" and
  // "night".
  IndexProcessor::Options options;
  options.max_tokens_per_document = 1000;

  ICING_ASSERT_OK_AND_ASSIGN(std::unique_ptr<Normalizer> normalizer,
                             Normalizer::Create(/*max_token_length=*/4));

  index_processor_ = std::make_unique<IndexProcessor>(
      schema_store_.get(), lang_segmenter_.get(), normalizer.get(),
      index_.get(), options);
  DocumentProto document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/1")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty), "hello world")
          .AddStringProperty(std::string(kPrefixedProperty), "good night moon!")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));

  // "good" should have been indexed normally.
  ICING_ASSERT_OK_AND_ASSIGN(std::unique_ptr<DocHitInfoIterator> itr,
                             index_->GetIterator("good", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kPrefixedSectionId})));

  // "night" should not have been.
  ICING_ASSERT_OK_AND_ASSIGN(itr,
                             index_->GetIterator("night", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)), IsEmpty());

  // "night" should have been truncated to "nigh".
  ICING_ASSERT_OK_AND_ASSIGN(itr,
                             index_->GetIterator("nigh", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kPrefixedSectionId})));
}

TEST_F(IndexProcessorTest, NonPrefixedContentPrefixQuery) {
  DocumentProto document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/1")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty), "best rocky movies")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));

  document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/2")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kPrefixedProperty), "rocky raccoon")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId1), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId1));

  // Only document_id 1 should surface in a prefix query for "Rock"
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("rock", kSectionIdMaskAll, TermMatchType::PREFIX));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId1, std::vector<SectionId>{kPrefixedSectionId})));
}

TEST_F(IndexProcessorTest, TokenNormalization) {
  DocumentProto document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/1")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty), "ALL UPPER CASE")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));

  document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/2")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty), "all lower case")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId1), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId1));

  ICING_ASSERT_OK_AND_ASSIGN(std::unique_ptr<DocHitInfoIterator> itr,
                             index_->GetIterator("case", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(
      GetHits(std::move(itr)),
      ElementsAre(EqualsDocHitInfo(kDocumentId1,
                                   std::vector<SectionId>{kExactSectionId}),
                  EqualsDocHitInfo(kDocumentId0,
                                   std::vector<SectionId>{kExactSectionId})));
}

TEST_F(IndexProcessorTest, OutOfOrderDocumentIds) {
  DocumentProto document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/1")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty), "ALL UPPER CASE")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId1), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId1));

  // Indexing a document with document_id < last_added_document_id should cause
  // a failure.
  document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/2")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty), "all lower case")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0),
              StatusIs(libtextclassifier3::StatusCode::INVALID_ARGUMENT));

  // As should indexing a document document_id == last_added_document_id.
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0),
              StatusIs(libtextclassifier3::StatusCode::INVALID_ARGUMENT));

  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId1));
}

TEST_F(IndexProcessorTest, NonAsciiIndexing) {
  DocumentProto document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/1")
          .SetSchema(std::string(kFakeType))
          .AddStringProperty(std::string(kExactProperty),
                             "你好，世界！你好：世界。“你好”世界？")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0), IsOk());
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));

  ICING_ASSERT_OK_AND_ASSIGN(std::unique_ptr<DocHitInfoIterator> itr,
                             index_->GetIterator("你好", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kExactSectionId})));
}

// TODO(b/142508211) Renable this test once a proper limit on max content length
// has been determined.
/*
TEST_F(IndexProcessorTest,
       LexiconFullIndexesSmallerTokensReturnsResourceExhausted) {
  IndexProcessor::Options processor_options;
  processor_options.max_tokens_per_document = 1000;
  processor_options.token_limit_behavior =
      IndexProcessor::Options::TokenLimitBehavior::kReturnError;
  index_processor_ = std::make_unique<IndexProcessor>(
      section_manager_.get(), lang_segmenter_.get(), normalizer_.get(),
      index_.get(), processor_options);

  // This is the maximum token length that an empty lexicon constructed for a
  // lite index with merge size of 1MiB can support.
  constexpr int kMaxTokenLength = 16777217;
  // Create a string "ppppppp..." with a length that is too large to fit into
  // the lexicon.
  std::string enormous_string(kMaxTokenLength + 1, 'p');
  DocumentProto document =
      DocumentBuilder()
          .SetKey("icing", "fake_type/1")
          .SetSchema(kFakeType)
          .AddStringProperty(std::string(kExactProperty),
                             absl_ports::StrCat(enormous_string, " foo"))
          .AddStringProperty(std::string(kPrefixedProperty), "bar baz")
          .Build();
  EXPECT_THAT(index_processor_->IndexDocument(document, kDocumentId0),
              StatusIs(libtextclassifier3::StatusCode::RESOURCE_EXHAUSTED));
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));

  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("foo", kSectionIdMaskAll, TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kExactSectionId})));

  ICING_ASSERT_OK_AND_ASSIGN(itr, index_->GetIterator("baz", kSectionIdMaskAll,
                                                TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kPrefixedSectionId})));
}
*/

}  // namespace

}  // namespace lib
}  // namespace icing
