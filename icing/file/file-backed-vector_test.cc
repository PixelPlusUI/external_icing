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

#include "icing/file/file-backed-vector.h"

#include <errno.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "icing/file/filesystem.h"
#include "icing/file/memory-mapped-file.h"
#include "icing/testing/common-matchers.h"
#include "icing/testing/tmp-directory.h"
#include "icing/util/crc32.h"
#include "icing/util/logging.h"

using ::testing::Eq;
using ::testing::Pointee;

namespace icing {
namespace lib {

namespace {

class FileBackedVectorTest : public testing::Test {
 protected:
  void SetUp() override {
    file_path_ = GetTestTempDir() + "/test.array";
    fd_ = filesystem_.OpenForWrite(file_path_.c_str());
    ASSERT_NE(-1, fd_);
    ASSERT_TRUE(filesystem_.Truncate(fd_, 0));
  }

  void TearDown() override {
    close(fd_);
    filesystem_.DeleteFile(file_path_.c_str());
  }

  // Helper method to loop over some data and insert into the vector at some idx
  template <typename T>
  void Insert(FileBackedVector<T>* vector, int32_t idx, std::string data) {
    for (int i = 0; i < data.length(); ++i) {
      ICING_ASSERT_OK(vector->Set(idx + i, data.at(i)));
    }
  }

  // Helper method to retrieve data from the beginning of the vector
  template <typename T>
  std::string_view Get(FileBackedVector<T>* vector, int32_t expected_len) {
    return Get(vector, 0, expected_len);
  }

  template <typename T>
  std::string_view Get(FileBackedVector<T>* vector, int32_t idx,
                       int32_t expected_len) {
    return std::string_view(vector->array() + idx, expected_len);
  }

  Filesystem filesystem_;
  std::string file_path_;
  int fd_;
};

TEST_F(FileBackedVectorTest, Create) {
  {
    // Create a vector for a new file
    ICING_ASSERT_OK_AND_ASSIGN(
        auto vector, FileBackedVector<char>::Create(
                         filesystem_, file_path_,
                         MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));
  }

  {
    // We can create it again based on the same file.
    ICING_ASSERT_OK_AND_ASSIGN(
        auto vector, FileBackedVector<char>::Create(
                         filesystem_, file_path_,
                         MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));
  }
}

TEST_F(FileBackedVectorTest, SimpleShared) {
  // Create a vector and add some data.
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FileBackedVector<char>> vector,
      FileBackedVector<char>::Create(
          filesystem_, file_path_,
          MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));
  EXPECT_THAT(vector->ComputeChecksum(), IsOkAndHolds(Crc32(0)));

  std::string expected = "abcde";
  Insert(vector.get(), 0, expected);
  EXPECT_EQ(expected.length(), vector->num_elements());
  EXPECT_EQ(expected, Get(vector.get(), expected.length()));

  uint32_t good_crc_value = 1134899064U;
  const Crc32 good_crc(good_crc_value);
  // Explicit call to update the crc does update the value
  EXPECT_THAT(vector->ComputeChecksum(), IsOkAndHolds(good_crc));

  // PersistToDisk does nothing bad.
  ICING_EXPECT_OK(vector->PersistToDisk());

  // Close out the old vector to ensure everything persists properly before we
  // reassign it
  vector.reset();

  // Write a bad crc, this would be a mismatch compared to the computed crc of
  // the contents on reinitialization.
  uint32_t bad_crc_value = 123;
  filesystem_.PWrite(file_path_.data(),
                     offsetof(FileBackedVector<char>::Header, vector_checksum),
                     &bad_crc_value, sizeof(bad_crc_value));

  ASSERT_THAT(FileBackedVector<char>::Create(
                  filesystem_, file_path_,
                  MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC),
              StatusIs(libtextclassifier3::StatusCode::INTERNAL));

  // Get it back into an ok state
  filesystem_.PWrite(file_path_.data(),
                     offsetof(FileBackedVector<char>::Header, vector_checksum),
                     &good_crc_value, sizeof(good_crc_value));
  ICING_ASSERT_OK_AND_ASSIGN(
      vector, FileBackedVector<char>::Create(
                  filesystem_, file_path_,
                  MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));

  EXPECT_EQ(expected, Get(vector.get(), expected.length()));

  // Close out the old vector to ensure everything persists properly before we
  // reassign it
  vector.reset();

  // Can reinitialize it safely
  ICING_ASSERT_OK_AND_ASSIGN(
      vector, FileBackedVector<char>::Create(
                  filesystem_, file_path_,
                  MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));

  // Truncate the content
  ICING_EXPECT_OK(vector->TruncateTo(0));

  // We don't automatically update the crc when we truncate.
  EXPECT_THAT(vector->ComputeChecksum(), IsOkAndHolds(good_crc));
  EXPECT_EQ(0u, vector->num_elements());
}

TEST_F(FileBackedVectorTest, Get) {
  // Create a vector and add some data.
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FileBackedVector<char>> vector,
      FileBackedVector<char>::Create(
          filesystem_, file_path_,
          MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));

  EXPECT_THAT(vector->ComputeChecksum(), IsOkAndHolds(Crc32(0)));

  std::string expected = "abc";
  Insert(vector.get(), 0, expected);
  EXPECT_EQ(expected.length(), vector->num_elements());

  EXPECT_THAT(vector->Get(0), IsOkAndHolds(Pointee(Eq('a'))));
  EXPECT_THAT(vector->Get(1), IsOkAndHolds(Pointee(Eq('b'))));
  EXPECT_THAT(vector->Get(2), IsOkAndHolds(Pointee(Eq('c'))));

  // Out of bounds error
  EXPECT_THAT(vector->Get(3),
              StatusIs(libtextclassifier3::StatusCode::OUT_OF_RANGE));
  EXPECT_THAT(vector->Get(-1),
              StatusIs(libtextclassifier3::StatusCode::OUT_OF_RANGE));
}

TEST_F(FileBackedVectorTest, IncrementalCrc_NonOverlappingChanges) {
  int num_elements = 1000;
  int incremental_size = 3;
  // Create an array with some data.
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FileBackedVector<char>> vector,
      FileBackedVector<char>::Create(
          filesystem_, file_path_,
          MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));

  Insert(vector.get(), 0, std::string(num_elements, 'a'));
  EXPECT_THAT(vector->ComputeChecksum(), IsOkAndHolds(Crc32(2620640643U)));

  // Non-overlapping changes to the array, with increasing intervals
  // between updating the checksum. Validate by mapping another array on top.
  uint32_t next_update = 2;
  for (uint32_t i = 0; i < num_elements; i += incremental_size) {
    Insert(vector.get(), i, std::string(incremental_size, 'b'));

    if (i >= next_update) {
      ICING_ASSERT_OK_AND_ASSIGN(Crc32 incremental_crc,
                                 vector->ComputeChecksum());
      ICING_LOG(INFO) << "Now crc @" << incremental_crc.Get();

      Crc32 full_crc;
      std::string_view reconstructed_view =
          std::string_view(vector->array(), vector->num_elements());
      full_crc.Append(reconstructed_view);

      ASSERT_EQ(incremental_crc, full_crc);
      next_update *= 2;
    }
  }

  for (uint32_t i = 0; i < num_elements; ++i) {
    EXPECT_THAT(vector->Get(i), IsOkAndHolds(Pointee(Eq('b'))));
  }
}

TEST_F(FileBackedVectorTest, IncrementalCrc_OverlappingChanges) {
  int num_elements = 1000;
  int incremental_size = 3;
  // Create an array with some data.
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FileBackedVector<char>> vector,
      FileBackedVector<char>::Create(
          filesystem_, file_path_,
          MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));

  Insert(vector.get(), 0, std::string(num_elements, 'a'));
  EXPECT_THAT(vector->ComputeChecksum(), IsOkAndHolds(Crc32(2620640643U)));

  // Overlapping changes to the array, with increasing intervals
  // between updating the checksum. Validate by mapping another array on top.
  uint32_t next_update = 2;
  for (uint32_t i = 0; i < num_elements; i++) {
    Insert(vector.get(), i, std::string(incremental_size, 'b'));

    if (i >= next_update) {
      ICING_ASSERT_OK_AND_ASSIGN(Crc32 incremental_crc,
                                 vector->ComputeChecksum());
      ICING_LOG(INFO) << "Now crc @" << incremental_crc.Get();

      Crc32 full_crc;
      std::string_view reconstructed_view =
          std::string_view(vector->array(), vector->num_elements());
      full_crc.Append(reconstructed_view);

      ASSERT_EQ(incremental_crc, full_crc);
      next_update *= 2;
    }
  }
  for (uint32_t i = 0; i < num_elements; ++i) {
    EXPECT_THAT(vector->Get(i), IsOkAndHolds(Pointee(Eq('b'))));
  }
}

TEST_F(FileBackedVectorTest, Grow) {
  // This is the same value as FileBackedVector::kMaxNumElts
  constexpr int32_t kMaxNumElts = 1U << 20;

  ASSERT_TRUE(filesystem_.Truncate(fd_, 0));

  // Create an array and add some data.
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FileBackedVector<char>> vector,
      FileBackedVector<char>::Create(
          filesystem_, file_path_,
          MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));
  EXPECT_THAT(vector->ComputeChecksum(), IsOkAndHolds(Crc32(0)));

  EXPECT_THAT(vector->Set(kMaxNumElts + 11, 'a'),
              StatusIs(libtextclassifier3::StatusCode::OUT_OF_RANGE));
  EXPECT_THAT(vector->Set(-1, 'a'),
              StatusIs(libtextclassifier3::StatusCode::OUT_OF_RANGE));

  uint32_t start = kMaxNumElts - 13;
  Insert(vector.get(), start, "abcde");

  // Crc works?
  const Crc32 good_crc(1134899064U);
  EXPECT_THAT(vector->ComputeChecksum(), IsOkAndHolds(good_crc));

  // PersistToDisk does nothing bad, and ensures the content is still there
  // after we recreate the vector
  ICING_EXPECT_OK(vector->PersistToDisk());

  // Close out the old vector to ensure everything persists properly before we
  // reassign it
  vector.reset();

  ICING_ASSERT_OK_AND_ASSIGN(
      vector, FileBackedVector<char>::Create(
                  filesystem_, file_path_,
                  MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));

  std::string expected = "abcde";
  EXPECT_EQ(expected, Get(vector.get(), start, expected.length()));
}

TEST_F(FileBackedVectorTest, GrowsInChunks) {
  // This is the same value as FileBackedVector::kGrowElements
  constexpr int32_t kGrowElements = 1U << 14;  // 16K

  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FileBackedVector<int>> vector,
      FileBackedVector<int>::Create(
          filesystem_, file_path_,
          MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));

  // Our initial file size should just be the size of the header
  EXPECT_THAT(filesystem_.GetFileSize(file_path_.c_str()),
              Eq(sizeof(FileBackedVector<char>::Header)));

  // Once we add something though, we'll grow to kGrowElements big
  Insert(vector.get(), 0, "a");
  EXPECT_THAT(filesystem_.GetFileSize(file_path_.c_str()),
              Eq(kGrowElements * sizeof(int)));

  // Should still be the same size, don't need to grow underlying file
  Insert(vector.get(), 1, "b");
  EXPECT_THAT(filesystem_.GetFileSize(file_path_.c_str()),
              Eq(kGrowElements * sizeof(int)));

  // Now we grow by a kGrowElements chunk, so the underlying file is 2
  // kGrowElements big
  Insert(vector.get(), 2, std::string(kGrowElements, 'c'));
  EXPECT_THAT(filesystem_.GetFileSize(file_path_.c_str()),
              Eq(kGrowElements * 2 * sizeof(int)));

  // Destroy/persist the contents.
  vector.reset();

  // Reinitialize
  ICING_ASSERT_OK_AND_ASSIGN(
      vector, FileBackedVector<int>::Create(
                  filesystem_, file_path_,
                  MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));

  // Should be the same file size as before
  EXPECT_THAT(filesystem_.GetFileSize(file_path_.c_str()),
              Eq(kGrowElements * 2 * sizeof(int)));
}

TEST_F(FileBackedVectorTest, Delete) {
  // Can delete even if there's nothing there
  ICING_EXPECT_OK(FileBackedVector<int64_t>::Delete(filesystem_, file_path_));

  // Create a vector and add some data.
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FileBackedVector<char>> vector,
      FileBackedVector<char>::Create(
          filesystem_, file_path_,
          MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));

  EXPECT_THAT(vector->ComputeChecksum(), IsOkAndHolds(Crc32(0)));

  std::string expected = "abcde";
  Insert(vector.get(), 0, expected);
  ASSERT_THAT(vector->ComputeChecksum(), IsOkAndHolds(Crc32(1134899064U)));
  ASSERT_EQ(expected.length(), vector->num_elements());

  // Close out the old vector to ensure everything persists properly before we
  // delete the underlying files
  vector.reset();

  ICING_EXPECT_OK(FileBackedVector<int64_t>::Delete(filesystem_, file_path_));

  EXPECT_FALSE(filesystem_.FileExists(file_path_.data()));

  // Can successfully create again.
  ICING_ASSERT_OK_AND_ASSIGN(
      vector, FileBackedVector<char>::Create(
                  filesystem_, file_path_,
                  MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));
}

TEST_F(FileBackedVectorTest, TruncateTo) {
  ICING_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FileBackedVector<char>> vector,
      FileBackedVector<char>::Create(
          filesystem_, file_path_,
          MemoryMappedFile::Strategy::READ_WRITE_AUTO_SYNC));
  EXPECT_THAT(vector->ComputeChecksum(), IsOkAndHolds(Crc32(0)));

  Insert(vector.get(), 0, "A");
  Insert(vector.get(), 1, "Z");

  EXPECT_EQ(2, vector->num_elements());
  EXPECT_THAT(vector->ComputeChecksum(), IsOkAndHolds(Crc32(1658635950)));

  // Modify 1 element, out of 2 total elements. 1/2 changes exceeds the partial
  // crc limit, so our next checksum call will recompute the entire vector's
  // checksum.
  Insert(vector.get(), 1, "J");
  // We'll ignore everything after the 1st element, so the full vector's
  // checksum will only include "J".
  ICING_EXPECT_OK(vector->TruncateTo(1));
  EXPECT_EQ(1, vector->num_elements());
  EXPECT_THAT(vector->ComputeChecksum(), IsOkAndHolds(Crc32(31158534)));

  // Truncating doesn't cause the checksum to be updated.
  ICING_EXPECT_OK(vector->TruncateTo(0));
  EXPECT_EQ(0, vector->num_elements());
  EXPECT_THAT(vector->ComputeChecksum(), IsOkAndHolds(Crc32(31158534)));

  // Can't truncate past end.
  EXPECT_THAT(vector->TruncateTo(100),
              StatusIs(libtextclassifier3::StatusCode::OUT_OF_RANGE));

  // Must be greater than or equal to 0
  EXPECT_THAT(vector->TruncateTo(-1),
              StatusIs(libtextclassifier3::StatusCode::OUT_OF_RANGE));
}

}  // namespace

}  // namespace lib
}  // namespace icing
