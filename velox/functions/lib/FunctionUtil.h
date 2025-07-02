/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <folly/Bits.h>

namespace facebook::velox::functions {
namespace detail {
template <typename T>
void read(const char*& input, T& value) {
  value = folly::loadUnaligned<T>(input);
  input += sizeof(T);
}

template <typename T>
T read(const char*& input) {
  T value = folly::loadUnaligned<T>(input);
  input += sizeof(T);
  return value;
}

template <typename T>
void write(T value, char*& out) {
  folly::storeUnaligned(out, value);
  out += sizeof(T);
}
} // namespace detail
} // namespace facebook::velox::functions
