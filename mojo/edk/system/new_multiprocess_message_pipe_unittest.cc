// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <unordered_map>
#include <utility>

#include "base/logging.h"
#include "base/strings/string_split.h"
#include "mojo/edk/test/multiprocess_test_base.h"
#include "mojo/public/c/system/types.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "testing/gtest/include/gtest/gtest.h"

// TODO: merge these with tests in multiprocess_message_pipe_unittest.cc

namespace mojo {
namespace edk {
namespace {

class PipesTest : public test::MultiprocessTestBase {
 protected:
  // Convenience class for tests which will control command-driven children.
  // See the CommandDrivenClient definition below.
  class ClientController {
   public:
    explicit ClientController(MojoHandle h) : h_(h) {}

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

// Echos the primordial channel until "exit".
DEFINE_TEST_CLIENT_WITH_PIPE(ChannelEchoClient, PipesTest, h) {
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
DEFINE_TEST_CLIENT_WITH_PIPE(EchoServiceClient, PipesTest, h) {
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
DEFINE_TEST_CLIENT_WITH_PIPE(EchoServiceFactoryClient, PipesTest, h) {
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
DEFINE_TEST_CLIENT_WITH_PIPE(CommandDrivenClient, PipesTest, h) {
  std::unordered_map<std::string, MojoHandle> named_pipes;
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

TEST_F(PipesTest, CreateMessagePipe) {
  CREATE_PIPE(p0, p1);
  VerifyTransmission(p0, p1, "hey man");
  VerifyTransmission(p1, p0, "slow down");
  VerifyTransmission(p0, p1, std::string(10 * 1024 * 1024, 'a'));
  VerifyTransmission(p1, p0, std::string(10 * 1024 * 1024, 'e'));
}

TEST_F(PipesTest, PassMessagePipeLocal) {
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

TEST_F(PipesTest, MultiprocessChannelPipe) {
  RUN_WITH_CHILD(ChannelEchoClient)
  ON_PIPE(h)
    VerifyEcho(h, "in an interstellar burst");
    VerifyEcho(h, "i am back to save the universe");
    VerifyEcho(h, std::string(10 * 1024 * 1024, 'o'));

    WriteString(h, "exit");
  END_CHILD()
}

TEST_F(PipesTest, PassMessagePipeCrossProcess) {
  RUN_WITH_CHILD(EchoServiceClient)
  ON_PIPE(h)
    CREATE_PIPE(p0, p1);

    // Pass one end of the pipe to the other process.
    WriteStringWithHandles(h, "here take this", &p1, 1);

    VerifyEcho(p0, "and you may ask yourself");
    VerifyEcho(p0, "where does that highway go?");
    VerifyEcho(p0, std::string(20 * 1024 * 1024, 'i'));

    WriteString(p0, "exit");
  END_CHILD()
}

TEST_F(PipesTest, PassMoarMessagePipesCrossProcess) {
  RUN_WITH_CHILD(EchoServiceFactoryClient)
  ON_PIPE(h)
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

TEST_F(PipesTest, ChannelPipesWithMultipleChildren) {
  RUN_WITH_CHILDREN("ChannelEchoClient", "ChannelEchoClient")
  ON_PIPES(h)
    VerifyEcho(h[0], "hello child 0");
    VerifyEcho(h[1], "hello child 1");

    WriteString(h[0], "exit");
    WriteString(h[1], "exit");
  END_CHILDREN()
}

TEST_F(PipesTest, ChildToChildPipes) {
  RUN_WITH_CHILDREN("CommandDrivenClient", "CommandDrivenClient")
  ON_PIPES(h)
    ClientController a(h[0]);
    ClientController b(h[1]);

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
  END_CHILDREN()
}

TEST_F(PipesTest, MoreChildToChildPipes) {
  RUN_WITH_CHILDREN("CommandDrivenClient", "CommandDrivenClient",
                    "CommandDrivenClient", "CommandDrivenClient")
  ON_PIPES(h)
    ClientController a(h[0]), b(h[1]), c(h[2]), d(h[3]);

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

    // Have |a| pass its new |d|-pipe to |b|. It will eventually connect to |c|.
    a.Send("pass:d_pipe:b_pipe");
    b.Send("catch:c_pipe:a_pipe");

    // Have |d| pass its new |a|-pipe to |c|. It will now be connected to |b|.
    d.Send("pass:a_pipe:c_pipe");
    c.Send("catch:b_pipe:d_pipe");

    // Make sure b and c and talk.
    b.Send("say:c_pipe:it's a beautiful day");
    c.Send("hear:b_pipe:it's a beautiful day");

    a.Exit();
    b.Exit();
    c.Exit();
    d.Exit();
  END_CHILDREN()
}

}  // namespace
}  // namespace edk
}  // namespace mojo
