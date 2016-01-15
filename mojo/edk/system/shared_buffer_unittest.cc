// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <string>
#include <utility>

#include "base/logging.h"
#include "base/strings/string_piece.h"
#include "mojo/edk/test/mojo_test_base.h"
#include "mojo/public/c/system/types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace edk {
namespace {

using SharedBufferTest = test::MojoTestBase;

// Reads a single message with a shared buffer handle, maps the buffer, copies
// the message contents into it, then exits.
DEFINE_TEST_CLIENT_WITH_PIPE(CopyToBufferClient, SharedBufferTest, h) {
  MojoHandle b;
  std::string message = ReadMessageWithHandles(h, &b, 1);
  WriteToBuffer(b, 0, message);
  return 0;
}

// Creates a new buffer, maps it, writes a message contents to it, unmaps it,
// and finally passes it back to the parent.
DEFINE_TEST_CLIENT_WITH_PIPE(CreateBufferClient, SharedBufferTest, h) {
  std::string message = ReadMessage(h);
  MojoHandle b = CreateBuffer(message.size());
  WriteToBuffer(b, 0, message);
  WriteMessageWithHandles(h, "have a buffer", &b, 1);
  return 0;
}

TEST_F(SharedBufferTest, CreateSharedBuffer) {
  const std::string message = "hello";
  MojoHandle h = CreateBuffer(message.size());
  WriteToBuffer(h, 0, message);
  ExpectBufferContents(h, 0, message);
}

TEST_F(SharedBufferTest, DuplicateSharedBuffer) {
  const std::string message = "hello";
  MojoHandle h = CreateBuffer(message.size());
  WriteToBuffer(h, 0, message);

  MojoHandle dupe = DuplicateBuffer(h);
  ExpectBufferContents(dupe, 0, message);
}

TEST_F(SharedBufferTest, PassSharedBufferLocal) {
  const std::string message = "hello";
  MojoHandle h = CreateBuffer(message.size());
  WriteToBuffer(h, 0, message);

  MojoHandle dupe = DuplicateBuffer(h);
  MojoHandle p0, p1;
  CreateMessagePipe(&p0, &p1);

  WriteMessageWithHandles(p0, "...", &dupe, 1);
  EXPECT_EQ("...", ReadMessageWithHandles(p1, &dupe, 1));

  ExpectBufferContents(dupe, 0, message);
}

TEST_F(SharedBufferTest, PassSharedBufferCrossProcess) {
  const std::string message = "hello";
  MojoHandle b = CreateBuffer(message.size());

  RUN_CHILD_ON_PIPE(CopyToBufferClient, h)
    MojoHandle dupe = DuplicateBuffer(b);
    WriteMessageWithHandles(h, message, &dupe, 1);
  END_CHILD()

  ExpectBufferContents(b, 0, message);
}

TEST_F(SharedBufferTest, PassSharedBufferFromChild) {
  const std::string message = "hello";
  MojoHandle b;
  RUN_CHILD_ON_PIPE(CreateBufferClient, h)
    WriteMessage(h, message);
    ReadMessageWithHandles(h, &b, 1);
  END_CHILD()

  ExpectBufferContents(b, 0, message);
}

}  // namespace
}  // namespace edk
}  // namespace mojo
