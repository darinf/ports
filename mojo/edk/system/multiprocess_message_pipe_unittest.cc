// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/containers/hash_tables.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "build/build_config.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/embedder/platform_shared_buffer.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "mojo/edk/system/dispatcher.h"
#include "mojo/edk/system/message_pipe_test_utils.h"
#include "mojo/edk/system/platform_handle_dispatcher.h"
#include "mojo/edk/system/raw_channel.h"
#include "mojo/edk/system/shared_buffer_dispatcher.h"
#include "mojo/edk/system/test_utils.h"
#include "mojo/edk/test/multiprocess_test_base.h"
#include "mojo/edk/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"


namespace mojo {
namespace edk {
namespace {

class MultiprocessMessagePipeTest
    : public test::MultiprocessMessagePipeTestBase {};

// TODO: Convert other tests to use the new multiprocess test base class.
class NewMultiprocessMessagePipeTest : public test::MultiprocessTestBase {
 protected:
  // Convenience class for tests which will control command-driven children.
  // See the CommandDrivenClient definition below.
  class CommandDrivenClientController {
   public:
    explicit CommandDrivenClientController(MojoHandle h) : h_(h) {}

    void Send(const std::string& command) {
      WriteString(h_, command);
      EXPECT_EQ("ok", ReadString(h_));
    }

    void SendHandle(const std::string& name, MojoHandle p) {
      WriteStringWithHandles(h_, "take:" + name, &p, 1);
      EXPECT_EQ("ok", ReadString(h_));
    }

    MojoHandle RetrieveHandle(const std::string& name) {
      WriteString(h_, "return:" + name);
      MojoHandle p;
      EXPECT_EQ("ok", ReadStringWithHandles(h_, &p, 1));
      return p;
    }

    void Exit() { WriteString(h_, "exit"); }

   private:
    MojoHandle h_;
  };
};

// For each message received, sends a reply message with the same contents
// repeated twice, until the other end is closed or it receives "quitquitquit"
// (which it doesn't reply to). It'll return the number of messages received,
// not including any "quitquitquit" message, modulo 100.
//
// TODO: Convert this client to use the DEFINE_TEST_CLIENT_WITH_PIPE macro.
MOJO_MULTIPROCESS_TEST_CHILD_MAIN(EchoEcho) {
  ScopedPlatformHandle client_platform_handle =
      std::move(test::MultiprocessTestHelper::client_platform_handle);
  CHECK(client_platform_handle.is_valid());
  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(client_platform_handle));

  const std::string quitquitquit("quitquitquit");
  int rv = 0;
  for (;; rv = (rv + 1) % 100) {
    // Wait for our end of the message pipe to be readable.
    HandleSignalsState hss;
    MojoResult result =
        MojoWait(mp.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
                 MOJO_DEADLINE_INDEFINITE, &hss);
    if (result != MOJO_RESULT_OK) {
      // It was closed, probably.
      CHECK_EQ(result, MOJO_RESULT_FAILED_PRECONDITION);
      CHECK_EQ(hss.satisfied_signals, MOJO_HANDLE_SIGNAL_PEER_CLOSED);
      CHECK_EQ(hss.satisfiable_signals, MOJO_HANDLE_SIGNAL_PEER_CLOSED);
      break;
    } else {
      CHECK((hss.satisfied_signals & MOJO_HANDLE_SIGNAL_READABLE));
      CHECK((hss.satisfiable_signals & MOJO_HANDLE_SIGNAL_READABLE));
    }

    std::string read_buffer(1000, '\0');
    uint32_t read_buffer_size = static_cast<uint32_t>(read_buffer.size());
    CHECK_EQ(MojoReadMessage(mp.get().value(), &read_buffer[0],
                             &read_buffer_size, nullptr,
                             0, MOJO_READ_MESSAGE_FLAG_NONE),
             MOJO_RESULT_OK);
    read_buffer.resize(read_buffer_size);
    VLOG(2) << "Child got: " << read_buffer;

    if (read_buffer == quitquitquit) {
      VLOG(2) << "Child quitting.";
      break;
    }

    std::string write_buffer = read_buffer + read_buffer;
    CHECK_EQ(MojoWriteMessage(mp.get().value(), write_buffer.data(),
                              static_cast<uint32_t>(write_buffer.size()),
                              nullptr, 0u, MOJO_WRITE_MESSAGE_FLAG_NONE),
             MOJO_RESULT_OK);
  }

   return rv;
}

// Echos the primordial channel until "exit".
DEFINE_TEST_CLIENT_WITH_PIPE(ChannelEchoClient, NewMultiprocessMessagePipeTest,
                             h) {
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
DEFINE_TEST_CLIENT_WITH_PIPE(EchoServiceClient, NewMultiprocessMessagePipeTest,
                             h) {
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
DEFINE_TEST_CLIENT_WITH_PIPE(EchoServiceFactoryClient, NewMultiprocessMessagePipeTest,
                             h) {
  MojoHandle p;
  ReadStringWithHandles(h, &p, 1);

  std::vector<MojoHandle> handles(2);
  handles[0] = h;
  handles[1] = p;
  std::vector<MojoHandleSignals> signals(2, MOJO_HANDLE_SIGNAL_READABLE);
  for (;;) {
    uint32_t index;
    CHECK_EQ(MojoWaitMany(handles.data(), signals.data(),
                          static_cast<uint32_t>(handles.size()),
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

// Parses commands from the parent pipe and does whatever it's asked to do.
DEFINE_TEST_CLIENT_WITH_PIPE(CommandDrivenClient,
                             NewMultiprocessMessagePipeTest, h) {
  base::hash_map<std::string, MojoHandle> named_pipes;
  for (;;) {
    MojoHandle p;
    auto parts = base::SplitString(ReadStringWithOptionalHandle(h, &p), ":",
                                   base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    CHECK(!parts.empty());
    std::string command = parts[0];
    if (command == "take") {
      // Take a pipe.
      CHECK_EQ(parts.size(), 2u);
      CHECK_NE(p, MOJO_HANDLE_INVALID);
      named_pipes[parts[1]] = p;
      WriteString(h, "ok");
    } else if (command == "return") {
      // Return a pipe.
      CHECK_EQ(parts.size(), 2u);
      CHECK_EQ(p, MOJO_HANDLE_INVALID);
      p = named_pipes[parts[1]];
      CHECK_NE(p, MOJO_HANDLE_INVALID);
      named_pipes.erase(parts[1]);
      WriteStringWithHandles(h, "ok", &p, 1);
    } else if (command == "say") {
      // Say something to a named pipe.
      CHECK_EQ(parts.size(), 3u);
      CHECK_EQ(p, MOJO_HANDLE_INVALID);
      p = named_pipes[parts[1]];
      CHECK_NE(p, MOJO_HANDLE_INVALID);
      CHECK(!parts[2].empty());
      WriteString(p, parts[2]);
      WriteString(h, "ok");
    } else if (command == "hear") {
      // Expect to read something from a named pipe.
      CHECK_EQ(parts.size(), 3u);
      CHECK_EQ(p, MOJO_HANDLE_INVALID);
      p = named_pipes[parts[1]];
      CHECK_NE(p, MOJO_HANDLE_INVALID);
      CHECK(!parts[2].empty());
      CHECK_EQ(parts[2], ReadString(p));
      WriteString(h, "ok");
    } else if (command == "pass") {
      // Pass one named pipe over another named pipe.
      CHECK_EQ(parts.size(), 3u);
      CHECK_EQ(p, MOJO_HANDLE_INVALID);
      p = named_pipes[parts[1]];
      MojoHandle carrier = named_pipes[parts[2]];
      CHECK_NE(p, MOJO_HANDLE_INVALID);
      CHECK_NE(carrier, MOJO_HANDLE_INVALID);
      named_pipes.erase(parts[1]);
      WriteStringWithHandles(carrier, "got a pipe for ya", &p, 1);
      WriteString(h, "ok");
    } else if (command == "catch") {
      // Expect to receive one named pipe from another named pipe.
      CHECK_EQ(parts.size(), 3u);
      CHECK_EQ(p, MOJO_HANDLE_INVALID);
      MojoHandle carrier = named_pipes[parts[2]];
      CHECK_NE(carrier, MOJO_HANDLE_INVALID);
      ReadStringWithHandles(carrier, &p, 1);
      CHECK_NE(p, MOJO_HANDLE_INVALID);
      named_pipes[parts[1]] = p;
      WriteString(h, "ok");
    } else if (command == "exit") {
      CHECK_EQ(parts.size(), 1u);
      break;
    }
  }
  return 0;
}

// Sends "hello" to child, and expects "hellohello" back.
#if defined(OS_ANDROID)
// Android multi-process tests are not executing the new process. This is flaky.
#define MAYBE_Basic DISABLED_Basic
#else
#define MAYBE_Basic Basic
#endif  // defined(OS_ANDROID)
TEST_F(MultiprocessMessagePipeTest, MAYBE_Basic) {
  helper()->StartChild("EchoEcho");

  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(helper()->server_platform_handle));

  std::string hello("hello");
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWriteMessage(mp.get().value(), hello.data(),
                             static_cast<uint32_t>(hello.size()), nullptr, 0u,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  HandleSignalsState hss;
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWait(mp.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
               MOJO_DEADLINE_INDEFINITE, &hss));
  // The child may or may not have closed its end of the message pipe and died
  // (and we may or may not know it yet), so our end may or may not appear as
  // writable.
  EXPECT_TRUE((hss.satisfied_signals & MOJO_HANDLE_SIGNAL_READABLE));
  EXPECT_TRUE((hss.satisfiable_signals & MOJO_HANDLE_SIGNAL_READABLE));

  std::string read_buffer(1000, '\0');
  uint32_t read_buffer_size = static_cast<uint32_t>(read_buffer.size());
  CHECK_EQ(MojoReadMessage(mp.get().value(), &read_buffer[0],
                           &read_buffer_size, nullptr, 0,
                           MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  read_buffer.resize(read_buffer_size);
  VLOG(2) << "Parent got: " << read_buffer;
  ASSERT_EQ(hello + hello, read_buffer);

  MojoClose(mp.release().value());

  // We sent one message.
  ASSERT_EQ(1 % 100, helper()->WaitForChildShutdown());
}

// Sends a bunch of messages to the child. Expects them "repeated" back. Waits
// for the child to close its end before quitting.
#if defined(OS_ANDROID)
// Android multi-process tests are not executing the new process. This is flaky.
#define MAYBE_QueueMessages DISABLED_QueueMessages
#else
#define MAYBE_QueueMessages QueueMessages
#endif  // defined(OS_ANDROID)
TEST_F(MultiprocessMessagePipeTest, MAYBE_QueueMessages) {
  helper()->StartChild("EchoEcho");

  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(helper()->server_platform_handle));

  static const size_t kNumMessages = 1001;
  for (size_t i = 0; i < kNumMessages; i++) {
    std::string write_buffer(i, 'A' + (i % 26));
    ASSERT_EQ(MOJO_RESULT_OK,
            MojoWriteMessage(mp.get().value(), write_buffer.data(),
                             static_cast<uint32_t>(write_buffer.size()),
                             nullptr, 0u, MOJO_WRITE_MESSAGE_FLAG_NONE));
  }

  for (size_t i = 0; i < kNumMessages; i++) {
    HandleSignalsState hss;
    ASSERT_EQ(MOJO_RESULT_OK,
              MojoWait(mp.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
                       MOJO_DEADLINE_INDEFINITE, &hss));
    // The child may or may not have closed its end of the message pipe and died
    // (and we may or may not know it yet), so our end may or may not appear as
    // writable.
    ASSERT_TRUE((hss.satisfied_signals & MOJO_HANDLE_SIGNAL_READABLE));
    ASSERT_TRUE((hss.satisfiable_signals & MOJO_HANDLE_SIGNAL_READABLE));

    std::string read_buffer(kNumMessages * 2, '\0');
    uint32_t read_buffer_size = static_cast<uint32_t>(read_buffer.size());
    ASSERT_EQ(MojoReadMessage(mp.get().value(), &read_buffer[0],
                             &read_buffer_size, nullptr, 0,
                             MOJO_READ_MESSAGE_FLAG_NONE),
             MOJO_RESULT_OK);
    read_buffer.resize(read_buffer_size);

    ASSERT_EQ(std::string(i * 2, 'A' + (i % 26)), read_buffer);
  }

  const std::string quitquitquit("quitquitquit");
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWriteMessage(mp.get().value(), quitquitquit.data(),
                             static_cast<uint32_t>(quitquitquit.size()),
                             nullptr, 0u, MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Wait for it to become readable, which should fail (since we sent
  // "quitquitquit").
  HandleSignalsState hss;
  ASSERT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            MojoWait(mp.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
                     MOJO_DEADLINE_INDEFINITE, &hss));
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfied_signals);
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfiable_signals);

  ASSERT_EQ(static_cast<int>(kNumMessages % 100),
            helper()->WaitForChildShutdown());
}

MOJO_MULTIPROCESS_TEST_CHILD_MAIN(CheckSharedBuffer) {
  ScopedPlatformHandle client_platform_handle =
      std::move(test::MultiprocessTestHelper::client_platform_handle);
  CHECK(client_platform_handle.is_valid());
  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(client_platform_handle));

  // Wait for the first message from our parent.
  HandleSignalsState hss;
  CHECK_EQ(MojoWait(mp.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
               MOJO_DEADLINE_INDEFINITE, &hss),
           MOJO_RESULT_OK);
  // In this test, the parent definitely doesn't close its end of the message
  // pipe before we do.
  CHECK_EQ(hss.satisfied_signals,
           MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_WRITABLE);
  CHECK_EQ(hss.satisfiable_signals, MOJO_HANDLE_SIGNAL_READABLE |
                                    MOJO_HANDLE_SIGNAL_WRITABLE |
                                    MOJO_HANDLE_SIGNAL_PEER_CLOSED);

  // It should have a shared buffer.
  std::string read_buffer(100, '\0');
  uint32_t num_bytes = static_cast<uint32_t>(read_buffer.size());
  MojoHandle handles[10];
  uint32_t num_handlers = MOJO_ARRAYSIZE(handles);  // Maximum number to receive
  CHECK_EQ(MojoReadMessage(mp.get().value(), &read_buffer[0],
                           &num_bytes, &handles[0],
                           &num_handlers, MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  read_buffer.resize(num_bytes);
  CHECK_EQ(read_buffer, std::string("go 1"));
  CHECK_EQ(num_handlers, 1u);

  // Make a mapping.
  void* buffer;
  CHECK_EQ(MojoMapBuffer(handles[0], 0, 100, &buffer,
                         MOJO_CREATE_SHARED_BUFFER_OPTIONS_FLAG_NONE),
           MOJO_RESULT_OK);

  // Write some stuff to the shared buffer.
  static const char kHello[] = "hello";
  memcpy(buffer, kHello, sizeof(kHello));

  // We should be able to close the dispatcher now.
  MojoClose(handles[0]);

  // And send a message to signal that we've written stuff.
  const std::string go2("go 2");
  CHECK_EQ(MojoWriteMessage(mp.get().value(), go2.data(),
                            static_cast<uint32_t>(go2.size()), nullptr, 0u,
                            MOJO_WRITE_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);

  // Now wait for our parent to send us a message.
  hss = HandleSignalsState();
  CHECK_EQ(MojoWait(mp.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
               MOJO_DEADLINE_INDEFINITE, &hss),
           MOJO_RESULT_OK);
  CHECK_EQ(hss.satisfied_signals,
           MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_WRITABLE);
  CHECK_EQ(hss.satisfiable_signals, MOJO_HANDLE_SIGNAL_READABLE |
                                        MOJO_HANDLE_SIGNAL_WRITABLE |
                                        MOJO_HANDLE_SIGNAL_PEER_CLOSED);

  read_buffer = std::string(100, '\0');
  num_bytes = static_cast<uint32_t>(read_buffer.size());
  CHECK_EQ(MojoReadMessage(mp.get().value(), &read_buffer[0], &num_bytes,
                           nullptr, 0, MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  read_buffer.resize(num_bytes);
  CHECK_EQ(read_buffer, std::string("go 3"));

  // It should have written something to the shared buffer.
  static const char kWorld[] = "world!!!";
  CHECK_EQ(memcmp(buffer, kWorld, sizeof(kWorld)), 0);

  // And we're done.

  return 0;
}

#if defined(OS_ANDROID)
// Android multi-process tests are not executing the new process. This is flaky.
#define MAYBE_SharedBufferPassing DISABLED_SharedBufferPassing
#else
#define MAYBE_SharedBufferPassing SharedBufferPassing
#endif
TEST_F(MultiprocessMessagePipeTest, MAYBE_SharedBufferPassing) {
  helper()->StartChild("CheckSharedBuffer");

  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(helper()->server_platform_handle));

  // Make a shared buffer.
  MojoCreateSharedBufferOptions options;
  options.struct_size = sizeof(options);
  options.flags = MOJO_CREATE_SHARED_BUFFER_OPTIONS_FLAG_NONE;

  MojoHandle shared_buffer;
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoCreateSharedBuffer(&options, 100, &shared_buffer));

  // Send the shared buffer.
  const std::string go1("go 1");

  MojoHandle duplicated_shared_buffer;
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoDuplicateBufferHandle(
                shared_buffer,
                nullptr,
                &duplicated_shared_buffer));
  MojoHandle handles[1];
  handles[0] = duplicated_shared_buffer;
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWriteMessage(mp.get().value(), &go1[0],
                             static_cast<uint32_t>(go1.size()), &handles[0],
                             MOJO_ARRAYSIZE(handles),
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Wait for a message from the child.
  HandleSignalsState hss;
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWait(mp.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
                     MOJO_DEADLINE_INDEFINITE, &hss));
  EXPECT_TRUE((hss.satisfied_signals & MOJO_HANDLE_SIGNAL_READABLE));
  EXPECT_TRUE((hss.satisfiable_signals & MOJO_HANDLE_SIGNAL_READABLE));

  std::string read_buffer(100, '\0');
  uint32_t num_bytes = static_cast<uint32_t>(read_buffer.size());
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoReadMessage(mp.get().value(), &read_buffer[0],
                             &num_bytes, nullptr, 0,
                             MOJO_READ_MESSAGE_FLAG_NONE));
  read_buffer.resize(num_bytes);
  ASSERT_EQ(std::string("go 2"), read_buffer);

  // After we get it, the child should have written something to the shared
  // buffer.
  static const char kHello[] = "hello";
  void* buffer;
  CHECK_EQ(MojoMapBuffer(shared_buffer, 0, 100, &buffer,
                         MOJO_CREATE_SHARED_BUFFER_OPTIONS_FLAG_NONE),
           MOJO_RESULT_OK);
  ASSERT_EQ(0, memcmp(buffer, kHello, sizeof(kHello)));

  // Now we'll write some stuff to the shared buffer.
  static const char kWorld[] = "world!!!";
  memcpy(buffer, kWorld, sizeof(kWorld));

  // And send a message to signal that we've written stuff.
  const std::string go3("go 3");
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWriteMessage(mp.get().value(), &go3[0],
                             static_cast<uint32_t>(go3.size()), nullptr, 0u,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Wait for |mp| to become readable, which should fail.
  hss = HandleSignalsState();
  ASSERT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            MojoWait(mp.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
                     MOJO_DEADLINE_INDEFINITE, &hss));
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfied_signals);
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfiable_signals);

  MojoClose(mp.release().value());

  ASSERT_EQ(0, helper()->WaitForChildShutdown());
}

MOJO_MULTIPROCESS_TEST_CHILD_MAIN(CheckPlatformHandleFile) {
  ScopedPlatformHandle client_platform_handle =
      std::move(test::MultiprocessTestHelper::client_platform_handle);
  CHECK(client_platform_handle.is_valid());
  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(client_platform_handle));

  HandleSignalsState hss;
  CHECK_EQ(MojoWait(mp.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
               MOJO_DEADLINE_INDEFINITE, &hss),
           MOJO_RESULT_OK);
  CHECK_EQ(hss.satisfied_signals,
           MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_WRITABLE);
  CHECK_EQ(hss.satisfiable_signals, MOJO_HANDLE_SIGNAL_READABLE |
                                        MOJO_HANDLE_SIGNAL_WRITABLE |
                                        MOJO_HANDLE_SIGNAL_PEER_CLOSED);

  std::string read_buffer(100, '\0');
  uint32_t num_bytes = static_cast<uint32_t>(read_buffer.size());
  MojoHandle handles[255];  // Maximum number to receive.
  uint32_t num_handlers = MOJO_ARRAYSIZE(handles);

  CHECK_EQ(MojoReadMessage(mp.get().value(), &read_buffer[0],
                           &num_bytes, &handles[0],
                           &num_handlers, MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  MojoClose(mp.release().value());

  read_buffer.resize(num_bytes);
  char hello[32];
  int num_handles = 0;
  sscanf(read_buffer.c_str(), "%s %d", hello, &num_handles);
  CHECK_EQ(std::string("hello"), std::string(hello));
  CHECK_GT(num_handles, 0);

  for (int i = 0; i < num_handles; ++i) {
    ScopedPlatformHandle h;
    CHECK_EQ(PassWrappedPlatformHandle(
                handles[i], &h),
             MOJO_RESULT_OK);
    CHECK(h.is_valid());
    MojoClose(handles[i]);

    base::ScopedFILE fp(test::FILEFromPlatformHandle(std::move(h), "r"));
    CHECK(fp);
    std::string fread_buffer(100, '\0');
    size_t bytes_read =
        fread(&fread_buffer[0], 1, fread_buffer.size(), fp.get());
    fread_buffer.resize(bytes_read);
    CHECK_EQ(fread_buffer, "world");
  }

  return 0;
}

class MultiprocessMessagePipeTestWithPipeCount
    : public test::MultiprocessMessagePipeTestBase,
      public testing::WithParamInterface<size_t> {};

TEST_P(MultiprocessMessagePipeTestWithPipeCount, PlatformHandlePassing) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  helper()->StartChild("CheckPlatformHandleFile");
  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(helper()->server_platform_handle));

  std::vector<MojoHandle> handles;

  size_t pipe_count = GetParam();
  for (size_t i = 0; i < pipe_count; ++i) {
    base::FilePath unused;
    base::ScopedFILE fp(
        CreateAndOpenTemporaryFileInDir(temp_dir.path(), &unused));
    const std::string world("world");
    CHECK_EQ(fwrite(&world[0], 1, world.size(), fp.get()), world.size());
    fflush(fp.get());
    rewind(fp.get());
    MojoHandle handle;
    ASSERT_EQ(
        CreatePlatformHandleWrapper(
            ScopedPlatformHandle(test::PlatformHandleFromFILE(std::move(fp))),
            &handle),
        MOJO_RESULT_OK);
    handles.push_back(handle);
  }

  char message[128];
  sprintf(message, "hello %d", static_cast<int>(pipe_count));
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWriteMessage(mp.get().value(), message,
                             static_cast<uint32_t>(strlen(message)),
                             &handles[0], static_cast<uint32_t>(handles.size()),
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Wait for it to become readable, which should fail.
  HandleSignalsState hss;
  ASSERT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            MojoWait(mp.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
                     MOJO_DEADLINE_INDEFINITE, &hss));
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfied_signals);
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfiable_signals);

  MojoClose(mp.release().value());

  ASSERT_EQ(0, helper()->WaitForChildShutdown());
}

// Android multi-process tests are not executing the new process. This is flaky.
#if !defined(OS_ANDROID)
INSTANTIATE_TEST_CASE_P(PipeCount,
                        MultiprocessMessagePipeTestWithPipeCount,
                        // TODO: Re-enable the 140-pipe case when ChannelPosix
                        // has support for sending lots of handles.
                        testing::Values(1u, 128u/*, 140u*/));
#endif

MOJO_MULTIPROCESS_TEST_CHILD_MAIN(CheckMessagePipe) {
  ScopedPlatformHandle client_platform_handle =
      std::move(test::MultiprocessTestHelper::client_platform_handle);
  CHECK(client_platform_handle.is_valid());

  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(client_platform_handle));

  // Wait for the first message from our parent.
  HandleSignalsState hss;
  CHECK_EQ(MojoWait(mp.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
               MOJO_DEADLINE_INDEFINITE, &hss),
           MOJO_RESULT_OK);
  // In this test, the parent definitely doesn't close its end of the message
  // pipe before we do.
  CHECK_EQ(hss.satisfied_signals,
           MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_WRITABLE);
  CHECK_EQ(hss.satisfiable_signals, MOJO_HANDLE_SIGNAL_READABLE |
                                    MOJO_HANDLE_SIGNAL_WRITABLE |
                                    MOJO_HANDLE_SIGNAL_PEER_CLOSED);

  // It should have a message pipe.
  MojoHandle handles[10];
  uint32_t num_handlers = MOJO_ARRAYSIZE(handles);
  CHECK_EQ(MojoReadMessage(mp.get().value(), nullptr,
                           nullptr, &handles[0],
                           &num_handlers, MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  CHECK_EQ(num_handlers, 1u);

  // Read data from the received message pipe.
  CHECK_EQ(MojoWait(handles[0], MOJO_HANDLE_SIGNAL_READABLE,
               MOJO_DEADLINE_INDEFINITE, &hss),
           MOJO_RESULT_OK);
  CHECK_EQ(hss.satisfied_signals,
           MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_WRITABLE);
  CHECK_EQ(hss.satisfiable_signals, MOJO_HANDLE_SIGNAL_READABLE |
                                    MOJO_HANDLE_SIGNAL_WRITABLE |
                                    MOJO_HANDLE_SIGNAL_PEER_CLOSED);

  std::string read_buffer(100, '\0');
  uint32_t read_buffer_size = static_cast<uint32_t>(read_buffer.size());
  CHECK_EQ(MojoReadMessage(handles[0], &read_buffer[0],
                           &read_buffer_size, nullptr,
                           0, MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  read_buffer.resize(read_buffer_size);
  CHECK_EQ(read_buffer, std::string("hello"));

  // Now write some data into the message pipe.
  std::string write_buffer = "world";
  CHECK_EQ(MojoWriteMessage(handles[0], write_buffer.data(),
                            static_cast<uint32_t>(write_buffer.size()), nullptr,
                            0u, MOJO_WRITE_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  MojoClose(handles[0]);
  return 0;
}

#if defined(OS_ANDROID)
// Android multi-process tests are not executing the new process. This is flaky.
#define MAYBE_MessagePipePassing DISABLED_MessagePipePassing
#else
#define MAYBE_MessagePipePassing MessagePipePassing
#endif
TEST_F(MultiprocessMessagePipeTest, MAYBE_MessagePipePassing) {
  helper()->StartChild("CheckMessagePipe");

  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(helper()->server_platform_handle));
  MojoCreateSharedBufferOptions options;
  options.struct_size = sizeof(options);
  options.flags = MOJO_CREATE_SHARED_BUFFER_OPTIONS_FLAG_NONE;

  MojoHandle mp1, mp2;
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoCreateMessagePipe(nullptr, &mp1, &mp2));

  // Write a string into one end of the new message pipe and send the other end.
  const std::string hello("hello");
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWriteMessage(mp1, &hello[0],
                             static_cast<uint32_t>(hello.size()), nullptr, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWriteMessage(mp.get().value(), nullptr, 0, &mp2, 1,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Wait for a message from the child.
  HandleSignalsState hss;
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWait(mp1, MOJO_HANDLE_SIGNAL_READABLE,
                     MOJO_DEADLINE_INDEFINITE, &hss));
  EXPECT_TRUE((hss.satisfied_signals & MOJO_HANDLE_SIGNAL_READABLE));
  EXPECT_TRUE((hss.satisfiable_signals & MOJO_HANDLE_SIGNAL_READABLE));

  std::string read_buffer(100, '\0');
  uint32_t read_buffer_size = static_cast<uint32_t>(read_buffer.size());
  CHECK_EQ(MojoReadMessage(mp1, &read_buffer[0],
                           &read_buffer_size, nullptr,
                           0, MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  read_buffer.resize(read_buffer_size);
  CHECK_EQ(read_buffer, std::string("world"));

  MojoClose(mp1);
  MojoClose(mp.release().value());

  ASSERT_EQ(0, helper()->WaitForChildShutdown());
}

// Like above test, but verifies passing the other MP handle works as well.
#if defined(OS_ANDROID)
// Android multi-process tests are not executing the new process. This is flaky.
#define MAYBE_MessagePipeTwoPassing DISABLED_MessagePipeTwoPassing
#else
#define MAYBE_MessagePipeTwoPassing MessagePipeTwoPassing
#endif
TEST_F(MultiprocessMessagePipeTest, MAYBE_MessagePipeTwoPassing) {
  helper()->StartChild("CheckMessagePipe");

  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(helper()->server_platform_handle));

  MojoHandle mp1, mp2;
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoCreateMessagePipe(nullptr, &mp2, &mp1));

  // Write a string into one end of the new message pipe and send the other end.
  const std::string hello("hello");
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWriteMessage(mp1, &hello[0],
                             static_cast<uint32_t>(hello.size()), nullptr, 0u,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWriteMessage(mp.get().value(), nullptr, 0u, &mp2, 1u,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Wait for a message from the child.
  HandleSignalsState hss;
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWait(mp1, MOJO_HANDLE_SIGNAL_READABLE,
                     MOJO_DEADLINE_INDEFINITE, &hss));
  EXPECT_TRUE((hss.satisfied_signals & MOJO_HANDLE_SIGNAL_READABLE));
  EXPECT_TRUE((hss.satisfiable_signals & MOJO_HANDLE_SIGNAL_READABLE));

  std::string read_buffer(100, '\0');
  uint32_t read_buffer_size = static_cast<uint32_t>(read_buffer.size());
  CHECK_EQ(MojoReadMessage(mp1, &read_buffer[0],
                           &read_buffer_size, nullptr,
                           0, MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  read_buffer.resize(read_buffer_size);
  CHECK_EQ(read_buffer, std::string("world"));

  MojoClose(mp.release().value());

  ASSERT_EQ(0, helper()->WaitForChildShutdown());
}

MOJO_MULTIPROCESS_TEST_CHILD_MAIN(DataPipeConsumer) {
  ScopedPlatformHandle client_platform_handle =
      std::move(test::MultiprocessTestHelper::client_platform_handle);
  CHECK(client_platform_handle.is_valid());

  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(client_platform_handle));

  // Wait for the first message from our parent.
  HandleSignalsState hss;
  CHECK_EQ(MojoWait(mp.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
               MOJO_DEADLINE_INDEFINITE, &hss),
           MOJO_RESULT_OK);
  // In this test, the parent definitely doesn't close its end of the message
  // pipe before we do.
  CHECK_EQ(hss.satisfied_signals,
           MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_WRITABLE);
  CHECK_EQ(hss.satisfiable_signals, MOJO_HANDLE_SIGNAL_READABLE |
                                    MOJO_HANDLE_SIGNAL_WRITABLE |
                                    MOJO_HANDLE_SIGNAL_PEER_CLOSED);

  // It should have a message pipe.
  MojoHandle handles[10];
  uint32_t num_handlers = MOJO_ARRAYSIZE(handles);
  CHECK_EQ(MojoReadMessage(mp.get().value(), nullptr,
                           nullptr, &handles[0],
                           &num_handlers, MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  CHECK_EQ(num_handlers, 1u);

  // Read data from the received message pipe.
  CHECK_EQ(MojoWait(handles[0], MOJO_HANDLE_SIGNAL_READABLE,
               MOJO_DEADLINE_INDEFINITE, &hss),
           MOJO_RESULT_OK);
  CHECK_EQ(hss.satisfied_signals,
           MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_WRITABLE);
  CHECK_EQ(hss.satisfiable_signals, MOJO_HANDLE_SIGNAL_READABLE |
                                    MOJO_HANDLE_SIGNAL_WRITABLE |
                                    MOJO_HANDLE_SIGNAL_PEER_CLOSED);

  std::string read_buffer(100, '\0');
  uint32_t read_buffer_size = static_cast<uint32_t>(read_buffer.size());
  CHECK_EQ(MojoReadMessage(handles[0], &read_buffer[0],
                           &read_buffer_size, nullptr,
                           0, MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  read_buffer.resize(read_buffer_size);
  CHECK_EQ(read_buffer, std::string("hello"));

  // Now write some data into the message pipe.
  std::string write_buffer = "world";
  CHECK_EQ(MojoWriteMessage(handles[0], write_buffer.data(),
                            static_cast<uint32_t>(write_buffer.size()),
                            nullptr, 0u, MOJO_WRITE_MESSAGE_FLAG_NONE),
            MOJO_RESULT_OK);
  MojoClose(handles[0]);
  return 0;
}

#if defined(OS_ANDROID)
// Android multi-process tests are not executing the new process. This is flaky.
#define MAYBE_DataPipeConsumer DISABLED_DataPipeConsumer
#else
#define MAYBE_DataPipeConsumer DataPipeConsumer
#endif
TEST_F(MultiprocessMessagePipeTest, MAYBE_DataPipeConsumer) {
  helper()->StartChild("DataPipeConsumer");

  ScopedMessagePipeHandle mp =
      CreateMessagePipe(std::move(helper()->server_platform_handle));
  MojoCreateSharedBufferOptions options;
  options.struct_size = sizeof(options);
  options.flags = MOJO_CREATE_SHARED_BUFFER_OPTIONS_FLAG_NONE;

  MojoHandle mp1, mp2;
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoCreateMessagePipe(nullptr, &mp2, &mp1));

  // Write a string into one end of the new message pipe and send the other end.
  const std::string hello("hello");
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWriteMessage(mp1, &hello[0],
                             static_cast<uint32_t>(hello.size()), nullptr, 0u,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWriteMessage(mp.get().value(), nullptr, 0, &mp2, 1u,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Wait for a message from the child.
  HandleSignalsState hss;
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWait(mp1, MOJO_HANDLE_SIGNAL_READABLE,
                     MOJO_DEADLINE_INDEFINITE, &hss));
  EXPECT_TRUE((hss.satisfied_signals & MOJO_HANDLE_SIGNAL_READABLE));
  EXPECT_TRUE((hss.satisfiable_signals & MOJO_HANDLE_SIGNAL_READABLE));

  std::string read_buffer(100, '\0');
  uint32_t read_buffer_size = static_cast<uint32_t>(read_buffer.size());
  CHECK_EQ(MojoReadMessage(mp1, &read_buffer[0],
                           &read_buffer_size, nullptr,
                           0, MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  read_buffer.resize(read_buffer_size);
  CHECK_EQ(read_buffer, std::string("world"));

  MojoClose(mp1);
  MojoClose(mp.release().value());

  ASSERT_EQ(0, helper()->WaitForChildShutdown());
}

TEST_F(NewMultiprocessMessagePipeTest, CreateMessagePipe) {
  CREATE_PIPE(p0, p1);
  VerifyTransmission(p0, p1, "hey man");
  VerifyTransmission(p1, p0, "slow down");
  VerifyTransmission(p0, p1, std::string(10 * 1024 * 1024, 'a'));
  VerifyTransmission(p1, p0, std::string(10 * 1024 * 1024, 'e'));
}

TEST_F(NewMultiprocessMessagePipeTest, PassMessagePipeLocal) {
  CREATE_PIPE(p0, p1);
  VerifyTransmission(p0, p1, "testing testing");
  VerifyTransmission(p1, p0, "one two three");

  CREATE_PIPE(p2, p3);
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

TEST_F(NewMultiprocessMessagePipeTest, MultiprocessChannelPipe) {
  RUN_CHILD_ON_PIPE(ChannelEchoClient, h)
    VerifyEcho(h, "in an interstellar burst");
    VerifyEcho(h, "i am back to save the universe");
    VerifyEcho(h, std::string(10 * 1024 * 1024, 'o'));

    WriteString(h, "exit");
  END_CHILD()
}

TEST_F(NewMultiprocessMessagePipeTest, PassMessagePipeCrossProcess) {
  RUN_CHILD_ON_PIPE(EchoServiceClient, h)
    CREATE_PIPE(p0, p1);

    // Pass one end of the pipe to the other process.
    WriteStringWithHandles(h, "here take this", &p1, 1);

    VerifyEcho(p0, "and you may ask yourself");
    VerifyEcho(p0, "where does that highway go?");
    VerifyEcho(p0, std::string(20 * 1024 * 1024, 'i'));

    WriteString(p0, "exit");
  END_CHILD()
}

TEST_F(NewMultiprocessMessagePipeTest, PassMoarMessagePipesCrossProcess) {
  RUN_CHILD_ON_PIPE(EchoServiceFactoryClient, h)
    CREATE_PIPE(echo_factory_proxy, echo_factory_request);
    WriteStringWithHandles(
        h, "gief factory naow plz", &echo_factory_request, 1);

    CREATE_PIPE(echo_proxy_a, echo_request_a);
    CREATE_PIPE(echo_proxy_b, echo_request_b);

    WriteStringWithHandles(echo_factory_proxy, "give me an echo service plz!",
                           &echo_request_a, 1);
    WriteStringWithHandles(echo_factory_proxy, "give me one too!",
                           &echo_request_b, 1);

    VerifyEcho(echo_proxy_a, "i came here for an argument");
    VerifyEcho(echo_proxy_a, "shut your festering gob");
    VerifyEcho(echo_proxy_a, "mumble mumble mumble");

    VerifyEcho(echo_proxy_b, "wubalubadubdub");
    VerifyEcho(echo_proxy_b, "wubalubadubdub");

    CREATE_PIPE(echo_proxy_c, echo_request_c);

    WriteStringWithHandles(echo_factory_proxy, "hook me up also thanks",
                           &echo_request_c, 1);

    VerifyEcho(echo_proxy_a, "the frobinators taste like frobinators");
    VerifyEcho(echo_proxy_b, "beep bop boop");
    VerifyEcho(echo_proxy_c, "zzzzzzzzzzzzzzzzzzzzzzzzzz");

    WriteString(h, "exit");
  END_CHILD()
}

TEST_F(NewMultiprocessMessagePipeTest, ChannelPipesWithMultipleChildren) {
  RUN_CHILD_ON_PIPE(ChannelEchoClient, a)
    RUN_CHILD_ON_PIPE(ChannelEchoClient, b)
      VerifyEcho(a, "hello child 0");
      VerifyEcho(b, "hello child 1");

      WriteString(a, "exit");
      WriteString(b, "exit");
    END_CHILD()
  END_CHILD()
}

TEST_F(NewMultiprocessMessagePipeTest, ChildToChildPipes) {
  RUN_CHILD_ON_PIPE(CommandDrivenClient, h0)
    RUN_CHILD_ON_PIPE(CommandDrivenClient, h1)
      CommandDrivenClientController a(h0);
      CommandDrivenClientController b(h1);

      // Create a pipe and pass each end to a different client.
      CREATE_PIPE(p0, p1);
      a.SendHandle("x", p0);
      b.SendHandle("y", p1);

      // Make sure they can talk.
      a.Send("say:x:hello sir");
      b.Send("hear:y:hello sir");

      b.Send("say:y:i love multiprocess pipes!");
      a.Send("hear:x:i love multiprocess pipes!");

      a.Exit();
      b.Exit();
    END_CHILD()
  END_CHILD()
}

TEST_F(NewMultiprocessMessagePipeTest, MoreChildToChildPipes) {
  RUN_CHILD_ON_PIPE(CommandDrivenClient, h0)
    RUN_CHILD_ON_PIPE(CommandDrivenClient, h1)
      RUN_CHILD_ON_PIPE(CommandDrivenClient, h2)
        RUN_CHILD_ON_PIPE(CommandDrivenClient, h3)
          CommandDrivenClientController a(h0), b(h1), c(h2), d(h3);

          // Connect a to b and c to d

          CREATE_PIPE(p0, p1);
          a.SendHandle("b_pipe", p0);
          b.SendHandle("a_pipe", p1);

          CREATE_PIPE(p2, p3);
          c.SendHandle("d_pipe", p2);
          d.SendHandle("c_pipe", p3);

          // Connect b to c via a and d
          CREATE_PIPE(p4, p5);
          a.SendHandle("d_pipe", p4);
          d.SendHandle("a_pipe", p5);

          // Have |a| pass its new |d|-pipe to |b|. It will eventually connect
          // to |c|.
          a.Send("pass:d_pipe:b_pipe");
          b.Send("catch:c_pipe:a_pipe");

          // Have |d| pass its new |a|-pipe to |c|. It will now be connected to
          // |b|.
          d.Send("pass:a_pipe:c_pipe");
          c.Send("catch:b_pipe:d_pipe");

          // Make sure b and c and talk.
          b.Send("say:c_pipe:it's a beautiful day");
          c.Send("hear:b_pipe:it's a beautiful day");

          a.Exit();
          b.Exit();
          c.Exit();
          d.Exit();
        END_CHILD()
      END_CHILD()
    END_CHILD()
  END_CHILD()
}

}  // namespace
}  // namespace edk
}  // namespace mojo
