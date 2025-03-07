// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright (c) 2005, Google Inc.
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

// ---
// Author: Sanjay Ghemawat
#include "config_for_unittests.h"

#include "addressmap-inl.h"

#include <stdlib.h>

#include <vector>
#include <set>
#include <random>
#include <algorithm>
#include <utility>

#include "gtest/gtest.h"

// pair of associated value and object size
typedef std::pair<int, size_t> ValueT;

struct PtrAndSize {
  std::unique_ptr<char> ptr;
  size_t size;

  PtrAndSize(char* p, size_t s) : ptr(p), size(s) {}
};

size_t SizeFunc(const ValueT& v) { return v.second; }

TEST(AddressMapUnittest, Basic) {
  constexpr int N = 100000;
  constexpr int kIters = 20;
  constexpr int kMaxRealSize = 49;
  // 100Mb to stress not finding previous object (AddressMap's cluster is 1Mb):
  constexpr size_t kMaxSize = 100 << 20;

  std::random_device rd;
  std::mt19937 rng(rd());

  // generates uniformly distributed size_t in range [0, n)
  auto uniform = [&] (size_t n) -> size_t {
    return std::uniform_int_distribution<size_t>{0, n-1}(rng);
  };

  std::vector<PtrAndSize> ptrs_and_sizes;
  ptrs_and_sizes.reserve(N);
  for (int i = 0; i < N; ++i) {
    size_t s = uniform(kMaxRealSize-1) + 1;
    ptrs_and_sizes.emplace_back(new char[s], s);
  }

  for (int x = 0; x < kIters; ++x) {
    printf("Iteration %d/%d...\n", x, kIters);

    // Permute pointers to get rid of allocation order issues
    std::shuffle(ptrs_and_sizes.begin(), ptrs_and_sizes.end(), rd);

    AddressMap<ValueT> map(malloc, free);
    const ValueT* result;
    const void* res_p;

    // Insert a bunch of entries
    for (int i = 0; i < N; ++i) {
      char* p = ptrs_and_sizes[i].ptr.get();
      ASSERT_FALSE(map.Find(p));
      int offs = uniform(ptrs_and_sizes[i].size);
      ASSERT_FALSE(map.FindInside(&SizeFunc, kMaxSize, p + offs, &res_p));
      map.Insert(p, std::make_pair(i, ptrs_and_sizes[i].size));
      ASSERT_TRUE(result = map.Find(p));
      ASSERT_EQ(result->first, i);
      ASSERT_TRUE(result = map.FindInside(&SizeFunc, kMaxRealSize, p + offs, &res_p));
      ASSERT_EQ(res_p, p);
      ASSERT_EQ(result->first, i);
      map.Insert(p, std::make_pair(i + N, ptrs_and_sizes[i].size));
      ASSERT_TRUE(result = map.Find(p));
      ASSERT_EQ(result->first, i + N);
    }

    // Delete the even entries
    for (int i = 0; i < N; i += 2) {
      void* p = ptrs_and_sizes[i].ptr.get();
      ValueT removed;
      ASSERT_TRUE(map.FindAndRemove(p, &removed));
      ASSERT_EQ(removed.first, i + N);
    }

    // Lookup the odd entries and adjust them
    for (int i = 1; i < N; i += 2) {
      char* p = ptrs_and_sizes[i].ptr.get();
      ASSERT_TRUE(result = map.Find(p));
      ASSERT_EQ(result->first, i + N);
      int offs = uniform(ptrs_and_sizes[i].size);
      ASSERT_TRUE(result = map.FindInside(&SizeFunc, kMaxRealSize, p + offs, &res_p));
      ASSERT_EQ(res_p, p);
      ASSERT_EQ(result->first, i + N);
      map.Insert(p, std::make_pair(i + 2*N, ptrs_and_sizes[i].size));
      ASSERT_TRUE(result = map.Find(p));
      ASSERT_EQ(result->first, i + 2*N);
    }

    // Insert even entries back
    for (int i = 0; i < N; i += 2) {
      char* p = ptrs_and_sizes[i].ptr.get();
      int offs = uniform(ptrs_and_sizes[i].size);
      ASSERT_TRUE(!map.FindInside(&SizeFunc, kMaxSize, p + offs, &res_p));
      map.Insert(p, std::make_pair(i + 2*N, ptrs_and_sizes[i].size));
      ASSERT_TRUE(result = map.Find(p));
      ASSERT_EQ(result->first, i + 2*N);
      ASSERT_TRUE(result = map.FindInside(&SizeFunc, kMaxRealSize, p + offs, &res_p));
      ASSERT_EQ(res_p, p);
      ASSERT_EQ(result->first, i + 2*N);
    }

    // Check all entries
    std::set<std::pair<const void*, int> > check_set;
    map.Iterate([&] (const void* ptr, ValueT* val) {
      check_set.insert(std::make_pair(ptr, val->first));
    });
    ASSERT_EQ(check_set.size(), N);
    for (int i = 0; i < N; ++i) {
      void* p = ptrs_and_sizes[i].ptr.get();
      check_set.erase(std::make_pair(p, i + 2*N));
      ASSERT_TRUE(result = map.Find(p));
      ASSERT_EQ(result->first, i + 2*N);
    }
    ASSERT_EQ(check_set.size(), 0);
  }
}
