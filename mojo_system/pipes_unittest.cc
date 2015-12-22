// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "mojo/edk/system/handle_signals_state.h"
#include "mojo/edk/system/message_pipe_test_utils.h"
#include "mojo/edk/system/test_utils.h"
#include "mojo/edk/test/test_utils.h"
#include "mojo/public/c/system/functions.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace edk {
namespace {

void CreatePipePair(MojoHandle *p0, MojoHandle* p1) {
  MojoCreateMessagePipe(nullptr, p0, p1);
  CHECK_NE(*p0, MOJO_HANDLE_INVALID);
  CHECK_NE(*p1, MOJO_HANDLE_INVALID);
}

void WriteString(MojoHandle mp, const std::string& message) {
  CHECK_EQ(MojoWriteMessage(mp, message.data(),
                            static_cast<uint32_t>(message.size()), nullptr,
                            0, MOJO_WRITE_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
}

std::string ReadString(MojoHandle mp) {
  uint32_t message_size = 0;
  CHECK_EQ(MojoReadMessage(mp, nullptr, &message_size, nullptr, nullptr,
                           MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_RESOURCE_EXHAUSTED);

  std::string message(message_size, 'x');
  CHECK_EQ(MojoReadMessage(mp, &message[0], &message_size, nullptr,
                           nullptr, MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  return message;
}

void WaitToRead(MojoHandle mp) {
  CHECK_EQ(MojoWait(mp, MOJO_HANDLE_SIGNAL_READABLE, MOJO_DEADLINE_INDEFINITE,
                   nullptr),
          MOJO_RESULT_OK);
}

class PipesTest : public test::MultiprocessMessagePipeTestBase {
 public:
  PipesTest() {}

 protected:
  void ExpectEcho(MojoHandle mp, const std::string& message) {
    WriteString(mp, message);
    WaitToRead(mp);
    EXPECT_EQ(message, ReadString(mp));
  }

  void SendExit(MojoHandle mp) {
    WriteString(mp, "exit");
  }
};

MOJO_MULTIPROCESS_TEST_CHILD_MAIN(ChannelEchoClient) {
  ScopedPlatformHandle client_platform_handle =
      std::move(test::MultiprocessTestHelper::client_platform_handle);
  CHECK(client_platform_handle.is_valid());
  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(client_platform_handle));

  MojoHandle h = mp.get().value();
  for (;;) {
    WaitToRead(h);
    std::string message = ReadString(h);
    if (message == "exit")
      break;
    WriteString(h, message);
  }

  return 0;
}

TEST_F(PipesTest, MultiprocessChannelDispatch) {
  helper()->StartChild("ChannelEchoClient");
  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(helper()->server_platform_handle));

  MojoHandle h = mp.get().value();
  ExpectEcho(h, "in an interstellar burst");
  ExpectEcho(h, "i am back to save the universe");
  ExpectEcho(h, std::string(10 * 1024 * 1024, 'o'));
  SendExit(h);
  EXPECT_EQ(0, helper()->WaitForChildShutdown());
}

TEST_F(PipesTest, CreateMessagePipe) {
  MojoHandle p0, p1;
  CreatePipePair(&p0, &p1);

  WriteString(p0, "hey man");
  WaitToRead(p1);
  EXPECT_EQ(ReadString(p1), "hey man");
}

}  // namespace
}  // namespace edk
}  // namespace mojo
