// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <string>
#include <utility>

#include "base/logging.h"
#include "base/strings/string_piece.h"
#include "mojo/public/c/system/buffer.h"
#include "mojo/public/c/system/types.h"
#include "ports/mojo_system/multiprocess_test_base.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace edk {
namespace {

class BuffersTest : public test::MultiprocessTestBase {
 protected:
  static MojoHandle CreateBuffer(uint64_t size) {
    MojoHandle h;
    EXPECT_EQ(MojoCreateSharedBuffer(nullptr, size, &h), MOJO_RESULT_OK);
    return h;
  }

  static MojoHandle DuplicateBuffer(MojoHandle h) {
    MojoHandle new_handle;
    EXPECT_EQ(MOJO_RESULT_OK,
              MojoDuplicateBufferHandle(h, nullptr, &new_handle));
    return new_handle;
  }

  static void WriteToBuffer(MojoHandle h, const base::StringPiece& s) {
    char* data;
    EXPECT_EQ(MOJO_RESULT_OK,
              MojoMapBuffer(h, 0, s.size(), reinterpret_cast<void**>(&data),
                            MOJO_MAP_BUFFER_FLAG_NONE));
    memcpy(data, s.data(), s.size());
    EXPECT_EQ(MOJO_RESULT_OK, MojoUnmapBuffer(static_cast<void*>(data)));
  }

  static void ExpectBufferContents(MojoHandle h, const base::StringPiece& s) {
    char* data;
    EXPECT_EQ(MOJO_RESULT_OK,
              MojoMapBuffer(h, 0, s.size(), reinterpret_cast<void**>(&data),
                            MOJO_MAP_BUFFER_FLAG_NONE));
    EXPECT_EQ(s, base::StringPiece(data, s.size()));
    EXPECT_EQ(MOJO_RESULT_OK, MojoUnmapBuffer(static_cast<void*>(data)));
  }
};

// Reads a single message with a shared buffer handle, maps the buffer, copies
// the message contents into it, then exits.
DEFINE_TEST_CLIENT_WITH_PIPE(MultiprocessSharedBufferClient, h) {
  MojoHandle buffer_handle;
  std::string message = ReadStringWithHandles(h, &buffer_handle, 1);
  char* data;
  EXPECT_EQ(MOJO_RESULT_OK,
            MojoMapBuffer(buffer_handle, 0, message.size(),
                          reinterpret_cast<void**>(&data),
                          MOJO_MAP_BUFFER_FLAG_NONE));
  memcpy(data, message.data(), message.size());
  EXPECT_EQ(MOJO_RESULT_OK, MojoUnmapBuffer(static_cast<void*>(data)));
  return 0;
}

TEST_F(BuffersTest, CreateSharedBuffer) {
  const std::string message = "hello";
  MojoHandle h = CreateBuffer(message.size());
  WriteToBuffer(h, message);
  ExpectBufferContents(h, message);
}

TEST_F(BuffersTest, DuplicateSharedBuffer) {
  const std::string message = "hello";
  MojoHandle h = CreateBuffer(message.size());
  WriteToBuffer(h, message);

  MojoHandle dupe = DuplicateBuffer(h);
  ExpectBufferContents(dupe, message);
}

TEST_F(BuffersTest, PassSharedBufferLocal) {
  const std::string message = "hello";
  MojoHandle h = CreateBuffer(message.size());
  WriteToBuffer(h, message);

  MojoHandle dupe = DuplicateBuffer(h);
  CREATE_PIPE(p0, p1);

  WriteStringWithHandles(p0, "...", &dupe, 1);
  EXPECT_EQ("...", ReadStringWithHandles(p1, &dupe, 1));

  ExpectBufferContents(dupe, message);
}

TEST_F(BuffersTest, PassSharedBufferCrossProcess) {
  const std::string message = "hello";
  MojoHandle b = CreateBuffer(message.size());

  RUN_WITH_CHILD(MultiprocessSharedBufferClient)
  ON_PIPE(h)
    MojoHandle dupe = DuplicateBuffer(b);
    WriteStringWithHandles(h, message, &dupe, 1);
  END_CHILD()

  ExpectBufferContents(b, message);
}

}  // namespace
}  // namespace edk
}  // namespace mojo