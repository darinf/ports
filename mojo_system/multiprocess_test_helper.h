// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_TEST_MULTIPROCESS_TEST_HELPER_H_
#define MOJO_EDK_TEST_MULTIPROCESS_TEST_HELPER_H_

#include <string>

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/process/process.h"
#include "base/test/multiprocess_test.h"
#include "base/test/test_timeouts.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "mojo/public/cpp/system/macros.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "testing/multiprocess_func_list.h"

namespace mojo {

namespace edk {
class PlatformChannelPair;

namespace test {

extern const char kBrokerHandleSwitch[];

class MultiprocessTestHelper {
 public:
  MultiprocessTestHelper();
  ~MultiprocessTestHelper();

  // Start a child process and run the "main" function "named" |test_child_name|
  // declared using |MOJO_MULTIPROCESS_TEST_CHILD_MAIN()| or
  // |MOJO_MULTIPROCESS_TEST_CHILD_TEST()| (below).
  void StartChild(
      const std::string& test_child_name,
      const base::Callback<void(ScopedMessagePipeHandle)>& callback);

  // Like |StartChild()|, but appends an extra switch (with ASCII value) to the
  // command line. (The switch must not already be present in the default
  // command line.)
  void StartChildWithExtraSwitch(
      const std::string& test_child_name,
      const std::string& switch_string,
      const std::string& switch_value,
      const base::Callback<void(ScopedMessagePipeHandle)>& callback);

  // Wait for the child process to terminate.
  // Returns the exit code of the child process. Note that, though it's declared
  // to be an |int|, the exit code is subject to mangling by the OS. E.g., we
  // usually return -1 on error in the child (e.g., if |test_child_name| was not
  // found), but this is mangled to 255 on Linux. You should only rely on codes
  // 0-127 being preserved, and -1 being outside the range 0-127.
  int WaitForChildShutdown();

  // Like |WaitForChildShutdown()|, but returns true on success (exit code of 0)
  // and false otherwise. You probably want to do something like
  // |EXPECT_TRUE(WaitForChildTestShutdown());|. Mainly for use with
  // |MOJO_MULTIPROCESS_TEST_CHILD_TEST()|.
  bool WaitForChildTestShutdown();

  // Runs the message loop until it's quit. UsEd by the parent only.
  void RunUntilQuit();

  // For use by |MOJO_MULTIPROCESS_TEST_CHILD_MAIN()| only:
  static void ChildSetup();

  static int RunChildAsyncMain(const base::Callback<int()>& main);

  // For use (and only valid) in the child process:
  static ScopedMessagePipeHandle child_message_pipe;

 private:
  // Called after child startup, once the embedder has given us a message pipe
  // to the child.
  void OnMessagePipeCreated(
      const base::Callback<void(ScopedMessagePipeHandle)>& callback,
      ScopedMessagePipeHandle message_pipe);

  // Used by the parent for running async tasks.
  base::MessageLoop message_loop_;
  base::RunLoop run_loop_;

  // Token used by parent and child nodes to establish a message pipe.
  std::string port_token_;

  // Used by the broker.
  scoped_ptr<PlatformChannelPair> broker_platform_channel_pair_;

  // Valid after |StartChild()| and before |WaitForChildShutdown()|.
  base::Process test_child_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(MultiprocessTestHelper);
};

// Use this to declare the child process's "main()" function for tests using
// |MultiprocessTestHelper|. It returns an |int|, which will be the process's
// exit code (but see the comment about |WaitForChildShutdown()|).
#define MOJO_MULTIPROCESS_TEST_CHILD_MAIN(test_child_name)      \
  int test_child_name##TestChildAsyncMain();                    \
  MULTIPROCESS_TEST_MAIN_WITH_SETUP(                            \
      test_child_name##TestChildMain,                           \
      test::MultiprocessTestHelper::ChildSetup) {               \
        return test::MultiprocessTestHelper::RunChildAsyncMain( \
            base::Bind(&test_child_name##TestChildAsyncMain));  \
      }                                                         \
      int test_child_name##TestChildAsyncMain()

}  // namespace test
}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_TEST_MULTIPROCESS_TEST_HELPER_H_
