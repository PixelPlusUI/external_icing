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

#ifndef ICING_UTIL_LOGGING_H_
#define ICING_UTIL_LOGGING_H_

#include "base/logging.h"

namespace icing {
namespace lib {

// TODO(samzheng): Change to TC3_VLOG and TC3_LOG
#define ICING_VLOG(severity) VLOG(severity)
#define ICING_LOG(severity) LOG(severity)

}  // namespace lib
}  // namespace icing

#endif  // ICING_UTIL_LOGGING_H_
