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

void WriteStringWithHandles(MojoHandle mp,
                            const std::string& message,
                            MojoHandle *handles,
                            uint32_t num_handles) {
  CHECK_EQ(MojoWriteMessage(mp, message.data(),
                            static_cast<uint32_t>(message.size()),
                            handles, num_handles, MOJO_WRITE_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
}

void WriteString(MojoHandle mp, const std::string& message) {
  WriteStringWithHandles(mp, message, nullptr, 0);
}

std::string ReadStringWithHandles(MojoHandle mp,
                                  MojoHandle* handles,
                                  uint32_t expected_num_handles) {
  uint32_t message_size = 0;
  uint32_t num_handles = 0;
  CHECK_EQ(MojoReadMessage(mp, nullptr, &message_size, nullptr, &num_handles,
                           MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_RESOURCE_EXHAUSTED);
  CHECK_EQ(expected_num_handles, num_handles);

  std::string message(message_size, 'x');
  CHECK_EQ(MojoReadMessage(mp, &message[0], &message_size, handles,
                           &num_handles, MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  CHECK_EQ(expected_num_handles, num_handles);

  return message;
}

std::string ReadString(MojoHandle mp) {
  return ReadStringWithHandles(mp, nullptr, 0);
}

void WaitToRead(MojoHandle mp) {
  CHECK_EQ(MojoWait(mp, MOJO_HANDLE_SIGNAL_READABLE, MOJO_DEADLINE_INDEFINITE,
                   nullptr),
          MOJO_RESULT_OK);
}

void VerifyTransmission(MojoHandle source,
                        MojoHandle dest,
                        const std::string& message) {
  WriteString(source, message);
  WaitToRead(dest);
  EXPECT_EQ(message, ReadString(dest));
}

void VerifyEcho(MojoHandle mp, const std::string& message) {
  VerifyTransmission(mp, mp, message);
}

void SendExitMessage(MojoHandle mp) {
  WriteString(mp, "exit");
}

class PipesTest : public test::MultiprocessMessagePipeTestBase {
 public:
  PipesTest() {}
};

// Echos the primordial channel until "exit".
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

// Receives a pipe handle from the primordial channel and echos on it until
// "exit". Used to test simple pipe transfer across processes via channels.
MOJO_MULTIPROCESS_TEST_CHILD_MAIN(EchoServiceClient) {
  ScopedPlatformHandle client_platform_handle =
      std::move(test::MultiprocessTestHelper::client_platform_handle);
  CHECK(client_platform_handle.is_valid());
  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(client_platform_handle));
  MojoHandle h = mp.get().value();

  WaitToRead(h);
  MojoHandle p;
  ReadStringWithHandles(h, &p, 1);
  for (;;) {
    WaitToRead(p);
    std::string message = ReadString(p);
    if (message == "exit")
      break;
    WriteString(p, message);
  }

  return 0;
}

TEST_F(PipesTest, MultiprocessChannelDispatch) {
  helper()->StartChild("ChannelEchoClient");
  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(helper()->server_platform_handle));
  MojoHandle h = mp.get().value();

  VerifyEcho(h, "in an interstellar burst");
  VerifyEcho(h, "i am back to save the universe");
  VerifyEcho(h, std::string(10 * 1024 * 1024, 'o'));

  SendExitMessage(h);
  EXPECT_EQ(0, helper()->WaitForChildShutdown());
}

TEST_F(PipesTest, CreateMessagePipe) {
  MojoHandle p0, p1;
  CreatePipePair(&p0, &p1);

  VerifyTransmission(p0, p1, "hey man");
  VerifyTransmission(p1, p0, "slow down");
  VerifyTransmission(p0, p1, std::string(10 * 1024 * 1024, 'a'));
  VerifyTransmission(p1, p0, std::string(10 * 1024 * 1024, 'e'));
}

TEST_F(PipesTest, PassMessagePipeLocal) {
  MojoHandle p0, p1;
  CreatePipePair(&p0, &p1);

  VerifyTransmission(p0, p1, "testing testing");
  VerifyTransmission(p1, p0, "one two three");

  MojoHandle p2, p3;
  CreatePipePair(&p2, &p3);

  VerifyTransmission(p2, p3, "testing testing");
  VerifyTransmission(p3, p2, "one two three");

  // Pass p2 over p0 to p1.
  const std::string message = "ceci n'est pas une pipe";
  WriteStringWithHandles(p0, message, &p2, 1);
  WaitToRead(p1);
  EXPECT_EQ(message, ReadStringWithHandles(p1, &p2, 1));

  // Verify that the received handle (now in p2) still works.
  VerifyTransmission(p2, p3, "Easy come, easy go; will you let me go?");
  VerifyTransmission(p3, p2, "Bismillah! NO! We will not let you go!");
}

TEST_F(PipesTest, PassMessagePipeCrossProcess) {
  helper()->StartChild("EchoServiceClient");
  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(helper()->server_platform_handle));
  MojoHandle h = mp.get().value();

  MojoHandle p0, p1;
  CreatePipePair(&p0, &p1);

  // Pass one end of the pipe to the other process.
  WriteStringWithHandles(h, "here take this", &p1, 1);

  VerifyEcho(p0, "and you may ask yourself");
  VerifyEcho(p0, "where does that highway go?");

  SendExitMessage(p0);
  EXPECT_EQ(0, helper()->WaitForChildShutdown());
}

}  // namespace
}  // namespace edk
}  // namespace mojo
