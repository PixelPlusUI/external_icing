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

#ifndef ICING_TEXT_CLASSIFIER_LIB3_UTILS_BASE_LOGGING_LEVELS_H_
#define ICING_TEXT_CLASSIFIER_LIB3_UTILS_BASE_LOGGING_LEVELS_H_

namespace libtextclassifier3 {
namespace logging {

enum LogSeverity {
  FATAL = 0,
  ERROR,
  WARNING,
  INFO,
};

}  // namespace logging
}  // namespace libtextclassifier3

#endif  // ICING_TEXT_CLASSIFIER_LIB3_UTILS_BASE_LOGGING_LEVELS_H_
