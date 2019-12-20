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

#include "icing/index/index.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "utils/base/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "icing/file/filesystem.h"
#include "icing/index/hit/doc-hit-info.h"
#include "icing/index/iterator/doc-hit-info-iterator.h"
#include "icing/legacy/index/icing-filesystem.h"
#include "icing/legacy/index/icing-mock-filesystem.h"
#include "icing/proto/term.pb.h"
#include "icing/schema/section.h"
#include "icing/store/document-id.h"
#include "icing/testing/common-matchers.h"
#include "icing/testing/random-string.h"
#include "icing/testing/tmp-directory.h"

namespace icing {
namespace lib {

namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::NiceMock;
using ::testing::Test;

class IndexTest : public Test {
 protected:
  void SetUp() override {
    index_dir_ = GetTestTempDir() + "/index_test/";
    Index::Options options(index_dir_, /*index_merge_size=*/1024 * 1024);
    ICING_ASSERT_OK_AND_ASSIGN(index_, Index::Create(options, &filesystem_));
  }

  void TearDown() override {
    filesystem_.DeleteDirectoryRecursively(index_dir_.c_str());
  }

  std::unique_ptr<Index> index_;
  std::string index_dir_;
  IcingFilesystem filesystem_;
};

constexpr DocumentId kDocumentId0 = 0;
constexpr DocumentId kDocumentId1 = 1;
constexpr DocumentId kDocumentId2 = 2;
constexpr SectionId kSectionId2 = 2;
constexpr SectionId kSectionId3 = 3;

std::vector<DocHitInfo> GetHits(std::unique_ptr<DocHitInfoIterator> iterator) {
  std::vector<DocHitInfo> infos;
  while (iterator->Advance().ok()) {
    infos.push_back(iterator->doc_hit_info());
  }
  return infos;
}

MATCHER_P2(EqualsDocHitInfo, document_id, sections, "") {
  const DocHitInfo& actual = arg;
  SectionIdMask section_mask = kSectionIdMaskNone;
  for (SectionId section : sections) {
    section_mask |= 1U << section;
  }
  *result_listener << "actual is {document_id=" << actual.document_id()
                   << ", section_mask=" << actual.hit_section_ids_mask()
                   << "}, but expected was {document_id=" << document_id
                   << ", section_mask=" << section_mask << "}.";
  return actual.document_id() == document_id &&
         actual.hit_section_ids_mask() == section_mask;
}

TEST_F(IndexTest, EmptyIndex) {
  // Assert
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("foo", kSectionIdMaskAll, TermMatchType::EXACT_ONLY));
  EXPECT_THAT(itr->Advance(),
              StatusIs(libtextclassifier3::StatusCode::NOT_FOUND));

  ICING_ASSERT_OK_AND_ASSIGN(
      itr,
      index_->GetIterator("foo", kSectionIdMaskAll, TermMatchType::EXACT_ONLY));
  EXPECT_THAT(itr->Advance(),
              StatusIs(libtextclassifier3::StatusCode::NOT_FOUND));

  EXPECT_THAT(index_->last_added_document_id(), Eq(kInvalidDocumentId));
}

TEST_F(IndexTest, AdvancePastEnd) {
  // Act
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::EXACT_ONLY);
  EXPECT_THAT(edit.AddHit("foo"), IsOk());

  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("bar", kSectionIdMaskAll, TermMatchType::EXACT_ONLY));
  EXPECT_THAT(itr->Advance(),
              StatusIs(libtextclassifier3::StatusCode::NOT_FOUND));
  EXPECT_THAT(itr->doc_hit_info(),
              EqualsDocHitInfo(kInvalidDocumentId, std::vector<SectionId>()));

  ICING_ASSERT_OK_AND_ASSIGN(
      itr,
      index_->GetIterator("foo", kSectionIdMaskAll, TermMatchType::EXACT_ONLY));
  EXPECT_THAT(itr->Advance(), IsOk());
  EXPECT_THAT(itr->Advance(),
              StatusIs(libtextclassifier3::StatusCode::RESOURCE_EXHAUSTED));
  EXPECT_THAT(itr->doc_hit_info(),
              EqualsDocHitInfo(kInvalidDocumentId, std::vector<SectionId>()));
}

TEST_F(IndexTest, SingleHitSingleTermIndex) {
  // Act
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::EXACT_ONLY);
  EXPECT_THAT(edit.AddHit("foo"), IsOk());

  // Assert
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("foo", kSectionIdMaskAll, TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kSectionId2})));

  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));
}

TEST_F(IndexTest, SingleHitMultiTermIndex) {
  // Act
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::EXACT_ONLY);
  EXPECT_THAT(edit.AddHit("foo"), IsOk());
  EXPECT_THAT(edit.AddHit("bar"), IsOk());

  // Assert
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("foo", kSectionIdMaskAll, TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kSectionId2})));

  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));
}

TEST_F(IndexTest, NoHitMultiTermIndex) {
  // Act
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::EXACT_ONLY);
  EXPECT_THAT(edit.AddHit("foo"), IsOk());
  EXPECT_THAT(edit.AddHit("bar"), IsOk());

  // Assert
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("baz", kSectionIdMaskAll, TermMatchType::EXACT_ONLY));
  EXPECT_THAT(itr->Advance(),
              StatusIs(libtextclassifier3::StatusCode::NOT_FOUND));
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));
}

TEST_F(IndexTest, MultiHitMultiTermIndex) {
  // Act
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::EXACT_ONLY);
  EXPECT_THAT(edit.AddHit("foo"), IsOk());

  edit = index_->Edit(kDocumentId1, kSectionId2, TermMatchType::EXACT_ONLY);
  EXPECT_THAT(edit.AddHit("bar"), IsOk());

  edit = index_->Edit(kDocumentId2, kSectionId3, TermMatchType::EXACT_ONLY);
  EXPECT_THAT(edit.AddHit("foo"), IsOk());

  // Assert
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("foo", kSectionIdMaskAll, TermMatchType::EXACT_ONLY));
  EXPECT_THAT(
      GetHits(std::move(itr)),
      ElementsAre(
          EqualsDocHitInfo(kDocumentId2, std::vector<SectionId>{kSectionId3}),
          EqualsDocHitInfo(kDocumentId0, std::vector<SectionId>{kSectionId2})));
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId2));
}

TEST_F(IndexTest, MultiHitSectionRestrict) {
  // Act
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::EXACT_ONLY);
  EXPECT_THAT(edit.AddHit("foo"), IsOk());

  edit = index_->Edit(kDocumentId1, kSectionId3, TermMatchType::EXACT_ONLY);
  EXPECT_THAT(edit.AddHit("foo"), IsOk());

  // Assert
  SectionIdMask desired_section = 1U << kSectionId2;
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("foo", desired_section, TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kSectionId2})));

  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId1));
}

TEST_F(IndexTest, SingleHitDedupeIndex) {
  // Act
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::EXACT_ONLY);
  EXPECT_THAT(edit.AddHit("foo"), IsOk());
  EXPECT_THAT(edit.AddHit("foo"), IsOk());

  // Assert
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("foo", kSectionIdMaskAll, TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kSectionId2})));

  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));
}

TEST_F(IndexTest, PrefixHit) {
  // Act
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::PREFIX);
  ASSERT_THAT(edit.AddHit("fool"), IsOk());

  // Assert
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("foo", kSectionIdMaskAll, TermMatchType::PREFIX));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kSectionId2})));

  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));
}

TEST_F(IndexTest, MultiPrefixHit) {
  // Act
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::PREFIX);
  ASSERT_THAT(edit.AddHit("fool"), IsOk());

  edit = index_->Edit(kDocumentId1, kSectionId3, TermMatchType::EXACT_ONLY);
  ASSERT_THAT(edit.AddHit("foo"), IsOk());

  // Assert
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("foo", kSectionIdMaskAll, TermMatchType::PREFIX));
  EXPECT_THAT(
      GetHits(std::move(itr)),
      ElementsAre(
          EqualsDocHitInfo(kDocumentId1, std::vector<SectionId>{kSectionId3}),
          EqualsDocHitInfo(kDocumentId0, std::vector<SectionId>{kSectionId2})));

  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId1));
}

TEST_F(IndexTest, NoExactHitInPrefixQuery) {
  // Act
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::EXACT_ONLY);
  ASSERT_THAT(edit.AddHit("fool"), IsOk());

  edit = index_->Edit(kDocumentId1, kSectionId3, TermMatchType::PREFIX);
  ASSERT_THAT(edit.AddHit("foo"), IsOk());

  // Assert
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("foo", kSectionIdMaskAll, TermMatchType::PREFIX));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId1, std::vector<SectionId>{kSectionId3})));
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId1));
}

TEST_F(IndexTest, PrefixHitDedupe) {
  // Act
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::PREFIX);
  ASSERT_THAT(edit.AddHit("foo"), IsOk());
  ASSERT_THAT(edit.AddHit("fool"), IsOk());

  // Assert
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("foo", kSectionIdMaskAll, TermMatchType::PREFIX));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kSectionId2})));
  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));
}

TEST_F(IndexTest, PrefixToString) {
  SectionIdMask id_mask = (1U << kSectionId2) | (1U << kSectionId3);
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("foo", id_mask, TermMatchType::PREFIX));
  EXPECT_THAT(itr->ToString(), Eq("0000000000001100:foo*"));

  ICING_ASSERT_OK_AND_ASSIGN(itr, index_->GetIterator("foo", kSectionIdMaskAll,
                                                      TermMatchType::PREFIX));
  EXPECT_THAT(itr->ToString(), Eq("1111111111111111:foo*"));

  ICING_ASSERT_OK_AND_ASSIGN(itr, index_->GetIterator("foo", kSectionIdMaskNone,
                                                      TermMatchType::PREFIX));
  EXPECT_THAT(itr->ToString(), Eq("0000000000000000:foo*"));
}

TEST_F(IndexTest, ExactToString) {
  SectionIdMask id_mask = (1U << kSectionId2) | (1U << kSectionId3);
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("foo", id_mask, TermMatchType::EXACT_ONLY));
  EXPECT_THAT(itr->ToString(), Eq("0000000000001100:foo"));

  ICING_ASSERT_OK_AND_ASSIGN(
      itr,
      index_->GetIterator("foo", kSectionIdMaskAll, TermMatchType::EXACT_ONLY));
  EXPECT_THAT(itr->ToString(), Eq("1111111111111111:foo"));

  ICING_ASSERT_OK_AND_ASSIGN(itr,
                             index_->GetIterator("foo", kSectionIdMaskNone,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(itr->ToString(), Eq("0000000000000000:foo"));
}

TEST_F(IndexTest, NonAsciiTerms) {
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::PREFIX);
  ASSERT_THAT(edit.AddHit("こんにちは"), IsOk());
  ASSERT_THAT(edit.AddHit("あなた"), IsOk());

  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("こんに", kSectionIdMaskAll, TermMatchType::PREFIX));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kSectionId2})));

  ICING_ASSERT_OK_AND_ASSIGN(itr,
                             index_->GetIterator("あなた", kSectionIdMaskAll,
                                                 TermMatchType::EXACT_ONLY));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kSectionId2})));
}

TEST_F(IndexTest, FullIndex) {
  // Make a smaller index so that it's easier to fill up.
  Index::Options options(index_dir_, /*index_merge_size=*/1024);
  ICING_ASSERT_OK_AND_ASSIGN(index_, Index::Create(options, &filesystem_));
  std::default_random_engine random;
  libtextclassifier3::Status status = libtextclassifier3::Status::OK;
  constexpr int kTokenSize = 5;
  DocumentId document_id = 0;
  std::vector<std::string> query_terms;
  while (status.ok()) {
    for (int i = 0; i < 100; ++i) {
      Index::Editor edit =
          index_->Edit(document_id, kSectionId2, TermMatchType::EXACT_ONLY);
      std::string term = RandomString(kAlNumAlphabet, kTokenSize, &random);
      status = edit.AddHit(term.c_str());
      if (i % 50 == 0) {
        // Remember one out of every fifty terms to query for later.
        query_terms.push_back(std::move(term));
      }
      if (!status.ok()) {
        break;
      }
    }
    ++document_id;
  }

  // Assert
  // Adding more hits should fail.
  Index::Editor edit =
      index_->Edit(document_id + 1, kSectionId2, TermMatchType::EXACT_ONLY);
  EXPECT_THAT(edit.AddHit("foo"),
              StatusIs(libtextclassifier3::StatusCode::RESOURCE_EXHAUSTED));
  EXPECT_THAT(edit.AddHit("bar"),
              StatusIs(libtextclassifier3::StatusCode::RESOURCE_EXHAUSTED));
  EXPECT_THAT(edit.AddHit("baz"),
              StatusIs(libtextclassifier3::StatusCode::RESOURCE_EXHAUSTED));

  for (const std::string& term : query_terms) {
    ICING_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<DocHitInfoIterator> itr,
        index_->GetIterator(term.c_str(), kSectionIdMaskAll,
                            TermMatchType::EXACT_ONLY));
    // Each query term should contain at least one hit - there may have been
    // other hits for this term that were added.
    EXPECT_THAT(itr->Advance(), IsOk());
  }
  EXPECT_THAT(index_->last_added_document_id(), Eq(document_id - 1));
}

TEST_F(IndexTest, IndexCreateIOFailure) {
  // Create the index with mock filesystem. By default, Mock will return false,
  // so the first attempted file operation will fail.
  NiceMock<IcingMockFilesystem> mock_filesystem;
  Index::Options options(index_dir_, /*index_merge_size=*/1024 * 1024);
  EXPECT_THAT(Index::Create(options, &mock_filesystem),
              StatusIs(libtextclassifier3::StatusCode::INTERNAL));
}

TEST_F(IndexTest, IndexCreateCorruptionFailure) {
  // Add some content to the index
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::PREFIX);
  ASSERT_THAT(edit.AddHit("foo"), IsOk());
  ASSERT_THAT(edit.AddHit("bar"), IsOk());

  // Close the index.
  index_.reset();

  // Corrrupt the index file.
  std::string hit_buffer_filename = index_dir_ + "/idx/lite.hb";
  ScopedFd sfd(filesystem_.OpenForWrite(hit_buffer_filename.c_str()));
  ASSERT_THAT(sfd.is_valid(), IsTrue());

  constexpr std::string_view kCorruptBytes = "ffffffffffffffffffffff";
  // The first page of the hit_buffer is taken up by the header. Overwrite the
  // first page of content.
  constexpr int kHitBufferStartOffset = 4096;
  ASSERT_THAT(filesystem_.PWrite(sfd.get(), kHitBufferStartOffset,
                                 kCorruptBytes.data(), kCorruptBytes.length()),
              IsTrue());

  // Recreate the index.
  Index::Options options(index_dir_, /*index_merge_size=*/1024 * 1024);
  EXPECT_THAT(Index::Create(options, &filesystem_),
              StatusIs(libtextclassifier3::StatusCode::DATA_LOSS));
}

TEST_F(IndexTest, IndexPersistence) {
  // Add some content to the index
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::PREFIX);
  ASSERT_THAT(edit.AddHit("foo"), IsOk());
  ASSERT_THAT(edit.AddHit("bar"), IsOk());
  EXPECT_THAT(index_->PersistToDisk(), IsOk());

  // Close the index.
  index_.reset();

  // Recreate the index.
  Index::Options options(index_dir_, /*index_merge_size=*/1024 * 1024);
  ICING_ASSERT_OK_AND_ASSIGN(index_, Index::Create(options, &filesystem_));

  // Check that the hits are present.
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DocHitInfoIterator> itr,
      index_->GetIterator("f", kSectionIdMaskAll, TermMatchType::PREFIX));
  EXPECT_THAT(GetHits(std::move(itr)),
              ElementsAre(EqualsDocHitInfo(
                  kDocumentId0, std::vector<SectionId>{kSectionId2})));

  EXPECT_THAT(index_->last_added_document_id(), Eq(kDocumentId0));
}

TEST_F(IndexTest, InvalidHitBufferSize) {
  Index::Options options(
      index_dir_, /*index_merge_size=*/std::numeric_limits<uint32_t>::max());
  EXPECT_THAT(Index::Create(options, &filesystem_),
              StatusIs(libtextclassifier3::StatusCode::INVALID_ARGUMENT));
}

TEST_F(IndexTest, ComputeChecksumSameBetweenCalls) {
  // Add some content to the index.
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::PREFIX);
  ASSERT_THAT(edit.AddHit("foo"), IsOk());

  Crc32 foo_checksum(757666244U);
  EXPECT_THAT(index_->ComputeChecksum(), Eq(foo_checksum));

  // Calling it again shouldn't change the checksum
  EXPECT_THAT(index_->ComputeChecksum(), Eq(foo_checksum));
}

TEST_F(IndexTest, ComputeChecksumSameAcrossInstances) {
  // Add some content to the index.
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::PREFIX);
  ASSERT_THAT(edit.AddHit("foo"), IsOk());

  Crc32 foo_checksum(757666244U);
  EXPECT_THAT(index_->ComputeChecksum(), Eq(foo_checksum));

  // Recreate the index, checksum should still be the same across instances
  index_.reset();
  Index::Options options(index_dir_, /*index_merge_size=*/1024 * 1024);
  ICING_ASSERT_OK_AND_ASSIGN(index_, Index::Create(options, &filesystem_));

  EXPECT_THAT(index_->ComputeChecksum(), Eq(foo_checksum));
}

TEST_F(IndexTest, ComputeChecksumChangesOnModification) {
  // Add some content to the index.
  Index::Editor edit =
      index_->Edit(kDocumentId0, kSectionId2, TermMatchType::PREFIX);
  ASSERT_THAT(edit.AddHit("foo"), IsOk());

  Crc32 foo_checksum(757666244U);
  EXPECT_THAT(index_->ComputeChecksum(), Eq(foo_checksum));

  // Modifying the index changes the checksum;
  EXPECT_THAT(edit.AddHit("bar"), IsOk());

  Crc32 foo_bar_checksum(1228959551U);
  EXPECT_THAT(index_->ComputeChecksum(), Eq(foo_bar_checksum));
}

}  // namespace

}  // namespace lib
}  // namespace icing
