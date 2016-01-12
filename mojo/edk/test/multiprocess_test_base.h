// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_TEST_MULTIPROCESS_TEST_BASE_H_
#define MOJO_EDK_TEST_MULTIPROCESS_TEST_BASE_H_

#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/test/multiprocess_test_helper.h"
#include "mojo/public/c/system/types.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace edk {
namespace test {

class MultiprocessTestBase : public testing::Test {
 public:
  MultiprocessTestBase();
  ~MultiprocessTestBase() override;

 protected:
  class ClientController {
   public:
    ClientController(const std::string& client_name,
                     MultiprocessTestBase* test);
    ~ClientController();

    MojoHandle pipe() const { return pipe_.get().value(); }
    int WaitForShutdown();

   private:
    friend class MultiprocessTestBase;

    MultiprocessTestBase* test_;
    MultiprocessTestHelper helper_;
    ScopedMessagePipeHandle pipe_;
    bool was_shutdown_ = false;

    DISALLOW_COPY_AND_ASSIGN(ClientController);
  };

  ClientController& StartClient(const std::string& client_name);

  template <typename HandlerFunc>
  void StartClientWithHandler(const std::string& client_name,
                              HandlerFunc handler) {
    ClientController& c = StartClient(client_name);
    int expected_exit_code = handler(c.pipe());
    EXPECT_EQ(expected_exit_code, c.WaitForShutdown());
  }

  // Creates a new pipe, returning endpoint handles in |p0| and |p1|.
  static void CreatePipe(MojoHandle* p0, MojoHandle* p1);

  // Writes a string to the pipe, transferring handles in the process.
  static void WriteStringWithHandles(MojoHandle mp,
                                     const std::string& message,
                                     const MojoHandle* handles,
                                     uint32_t num_handles);

  // Writes a string to the pipe with no handles.
  static void WriteString(MojoHandle mp, const std::string& message);

  // Reads a string from the pipe, expecting to read an exact number of handles
  // in the process. Returns the read string.
  static std::string ReadStringWithHandles(MojoHandle mp,
                                           MojoHandle* handles,
                                           uint32_t expected_num_handles);

  // Reads a string from the pipe, expecting either zero or one handles.
  // If no handle is read, |handle| will be reset.
  static std::string ReadStringWithOptionalHandle(MojoHandle mp,
                                                  MojoHandle* handle);

  // Reads a string from the pipe, expecting to read no handles.
  // Returns the string.
  static std::string ReadString(MojoHandle mp);

  // Reads a string from the pipe, expecting to read no handles and exactly
  // |num_bytes| bytes, which are read into |data|.
  static void ReadString(MojoHandle mp, char* data, size_t num_bytes);

  // Writes |message| to |in| and expects to read it back from |out|.
  static void VerifyTransmission(MojoHandle in,
                                 MojoHandle out,
                                 const std::string& message);

  // Writes |message| to |mp| and expects to read it back from the same handle.
  static void VerifyEcho(MojoHandle mp, const std::string& message);

 private:
  friend class ClientController;

  std::vector<scoped_ptr<ClientController>> clients_;

  DISALLOW_COPY_AND_ASSIGN(MultiprocessTestBase);
};

#define CREATE_PIPE(a, b) MojoHandle a, b; CreatePipe(&a, &b);

#define RUN_CHILD_ON_PIPE(client_name, pipe_name) \
    StartClientWithHandler(#client_name, [&](MojoHandle pipe_name) -> int {

#define END_CHILD() return 0; });

#define END_CHILD_AND_EXPECT_EXIT_CODE(code) return code; });

// Use this to declare the child process's "main()" function for tests using
// MultiprocessTestBase and MultiprocessTestHelper. It returns an |int|, which
// will be the process's exit code (but see the comment about
// WaitForChildShutdown()).
//
// The function is defined as a static member of a subclass of
// MultiprocessTestBase so code within it has access to that class's static
// static helpers.
#define DEFINE_TEST_CLIENT_WITH_PIPE(test_child_name, test_base, pipe_name) \
  class test_child_name##_MainFixture : public test_base {                  \
   public:                                                                  \
    static int ClientMain(MojoHandle pipe);                                 \
    static int ClientMainWrapper(ScopedMessagePipeHandle mp) {              \
      return ClientMain(mp.get().value());                                  \
    }                                                                       \
  };                                                                        \
  MULTIPROCESS_TEST_MAIN_WITH_SETUP(                                        \
      test_child_name##TestChildMain,                                       \
      test::MultiprocessTestHelper::ChildSetup) {                           \
        return test_child_name##_MainFixture::ClientMainWrapper(            \
            std::move(test::MultiprocessTestHelper::client_message_pipe));  \
      }                                                                     \
      int test_child_name##_MainFixture::ClientMain(MojoHandle pipe_name)

}  // namespace test
}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_TEST_MULTIPROCESS_TEST_BASE_H_
