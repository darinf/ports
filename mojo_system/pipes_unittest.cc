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

void CreateMessagePipe(MojoHandle *p0, MojoHandle* p1) {
  MojoCreateMessagePipe(nullptr, p0, p1);
  CHECK_NE(*p0, MOJO_HANDLE_INVALID);
  CHECK_NE(*p1, MOJO_HANDLE_INVALID);
}

void WriteStringWithHandles(MojoHandle mp,
                            const std::string& message,
                            MojoHandle *handles,
                            uint32_t num_handles) {
  CHECK_EQ(MojoWait(mp, MOJO_HANDLE_SIGNAL_WRITABLE, MOJO_DEADLINE_INDEFINITE,
                    nullptr),
           MOJO_RESULT_OK);

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
  CHECK_EQ(MojoWait(mp, MOJO_HANDLE_SIGNAL_READABLE, MOJO_DEADLINE_INDEFINITE,
                    nullptr),
           MOJO_RESULT_OK);

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

void VerifyTransmission(MojoHandle source,
                        MojoHandle dest,
                        const std::string& message) {
  WriteString(source, message);

  // We don't use EXPECT_EQ; failures on really long messages make life hard.
  EXPECT_TRUE(message == ReadString(dest));
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

  MojoHandle p;
  ReadStringWithHandles(h, &p, 1);
  for (;;) {
    std::string message = ReadString(p);
    if (message == "exit")
      break;
    WriteString(p, message);
  }

  return 0;
}

// Receives a pipe handle from the primordial channel and reads new handles
// from it. Each read handle establishes a new echo channel.
MOJO_MULTIPROCESS_TEST_CHILD_MAIN(EchoServiceFactoryClient) {
  ScopedPlatformHandle client_platform_handle =
      std::move(test::MultiprocessTestHelper::client_platform_handle);
  CHECK(client_platform_handle.is_valid());
  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(client_platform_handle));
  MojoHandle h = mp.get().value();

  MojoHandle p;
  ReadStringWithHandles(h, &p, 1);

  std::vector<MojoHandle> handles(2);
  handles[0] = h;
  handles[1] = p;
  std::vector<MojoHandleSignals> signals(2, MOJO_HANDLE_SIGNAL_READABLE);
  for (;;) {
    uint32_t index;
    CHECK_EQ(MojoWaitMany(handles.data(), signals.data(), handles.size(),
                          MOJO_DEADLINE_INDEFINITE, &index, nullptr),
             MOJO_RESULT_OK);
    DCHECK_LE(index, handles.size());
    if (index == 0) {
      // If data is available on the first pipe, it should be an exit command.
      EXPECT_EQ(std::string("exit"), ReadString(h));
      break;
    } else if (index == 1) {
      // If the second pipe, it should be a new handle requesting echo service.
      MojoHandle echo_request;
      ReadStringWithHandles(p, &echo_request, 1);
      handles.push_back(echo_request);
      signals.push_back(MOJO_HANDLE_SIGNAL_READABLE);
    } else {
      // Otherwise it was one of our established echo pipes. Echo!
      WriteString(handles[index], ReadString(handles[index]));
    }
  }

  return 0;
}

TEST_F(PipesTest, MultiprocessChannelPipe) {
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
  CreateMessagePipe(&p0, &p1);

  VerifyTransmission(p0, p1, "hey man");
  VerifyTransmission(p1, p0, "slow down");
  VerifyTransmission(p0, p1, std::string(10 * 1024 * 1024, 'a'));
  VerifyTransmission(p1, p0, std::string(10 * 1024 * 1024, 'e'));
}

TEST_F(PipesTest, PassMessagePipeLocal) {
  MojoHandle p0, p1;
  CreateMessagePipe(&p0, &p1);

  VerifyTransmission(p0, p1, "testing testing");
  VerifyTransmission(p1, p0, "one two three");

  MojoHandle p2, p3;
  CreateMessagePipe(&p2, &p3);

  VerifyTransmission(p2, p3, "testing testing");
  VerifyTransmission(p3, p2, "one two three");

  // Pass p2 over p0 to p1.
  const std::string message = "ceci n'est pas une pipe";
  WriteStringWithHandles(p0, message, &p2, 1);
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
  CreateMessagePipe(&p0, &p1);

  // Pass one end of the pipe to the other process.
  WriteStringWithHandles(h, "here take this", &p1, 1);

  VerifyEcho(p0, "and you may ask yourself");
  VerifyEcho(p0, "where does that highway go?");
  VerifyEcho(p0, std::string(20 * 1024 * 1024, 'i'));

  SendExitMessage(p0);
  EXPECT_EQ(0, helper()->WaitForChildShutdown());
}

TEST_F(PipesTest, PassMoarMessagePipesCrossProcess) {
  helper()->StartChild("EchoServiceFactoryClient");
  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(helper()->server_platform_handle));
  MojoHandle h = mp.get().value();

  MojoHandle echo_factory_proxy, echo_factory_request;
  CreateMessagePipe(&echo_factory_proxy, &echo_factory_request);

  WriteStringWithHandles(h, "gief factory naow plz", &echo_factory_request, 1);

  MojoHandle echo_proxy_a, echo_request_a;
  CreateMessagePipe(&echo_proxy_a, &echo_request_a);

  MojoHandle echo_proxy_b, echo_request_b;
  CreateMessagePipe(&echo_proxy_b, &echo_request_b);

  WriteStringWithHandles(echo_factory_proxy, "give me an echo service plz!",
                         &echo_request_a, 1);
  WriteStringWithHandles(echo_factory_proxy, "give me one too!",
                         &echo_request_b, 1);

  VerifyEcho(echo_proxy_a, "i came here for an argument");
  VerifyEcho(echo_proxy_a, "shut your festering gob");
  VerifyEcho(echo_proxy_a, "oh sorry, this is abuse. argument's down the hall");

  VerifyEcho(echo_proxy_b, "wubalubadubdub");
  VerifyEcho(echo_proxy_b, "wubalubadubdub");

  MojoHandle echo_proxy_c, echo_request_c;
  CreateMessagePipe(&echo_proxy_c, &echo_request_c);

  WriteStringWithHandles(echo_factory_proxy, "hook me up also thanks",
                         &echo_request_c, 1);

  VerifyEcho(echo_proxy_a, "the frobinators taste like frobinators");
  VerifyEcho(echo_proxy_b, "beep bop boop");
  VerifyEcho(echo_proxy_c, "zzzzzzzzzzzzzzzzzzzzzzzzzz");

  SendExitMessage(h);
  EXPECT_EQ(0, helper()->WaitForChildShutdown());
}

}  // namespace
}  // namespace edk
}  // namespace mojo
