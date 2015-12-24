// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_MULTIPROCESS_TEST_BASE_H_
#define PORTS_MOJO_SYSTEM_MULTIPROCESS_TEST_BASE_H_

#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/macros.h"
#include "mojo/public/c/system/types.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "ports/mojo_system/multiprocess_test_helper.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace edk {
namespace test {

class MultiprocessTestBase : public testing::Test {
 public:
  MultiprocessTestBase();
  ~MultiprocessTestBase() override;

 protected:
  // These helpers let tests call RunChild/RunChildren with lambdas.

  template <typename FuncType>
  static void CallPipeHandler(FuncType f, ScopedMessagePipeHandle mp) {
    f(std::move(mp));
  }

  template <typename FuncType>
  static base::Callback<void(ScopedMessagePipeHandle)>
  BindPipeHandler(FuncType f) {
    return base::Bind(&CallPipeHandler<FuncType>, f);
  }

  template <typename FuncType>
  static void CallPipesHandler(
      FuncType f,
      const std::vector<ScopedMessagePipeHandle>* mps) { f(mps); }

  template <typename FuncType>
  static base::Callback<void(const std::vector<ScopedMessagePipeHandle>*)>
  BindPipesHandler(FuncType f) {
    return base::Bind(&CallPipesHandler<FuncType>, f);
  }

  // Runs a new child process using |client_name| for the child test client.
  // |callback| is invoked with a message pipe handle connected to
  // |test::MultiprocessTestHelper::child_message_pipe| in the child process.
  //
  // RunChild does not return until the child process exits.
  //
  // Use the RUN_CHILD (et al) macro below for convenience.
  void RunChildWithCallback(
      const std::string& client_name,
      const base::Callback<void(ScopedMessagePipeHandle)>& callback);

  template <typename CallbackType>
  void RunChild(const std::string& client_name, const CallbackType& callback) {
    RunChildWithCallback(
        client_name,
        BindPipeHandler([callback](ScopedMessagePipeHandle mp) {
          callback(mp.get().value()); }));
  }

  // Runs N child processes for N arguments, where each argument is the name
  // of a child test client to run. |callback| is invoked with N message
  // pipe handles, connected to
  // |test::MultiprocessTestHelper::child_message_pipe| in each child process.
  //
  // RunChildren does not return until all children exit.
  //
  // Use the RUN_CHILDREN (et al) macro below for convenience.
  void RunChildrenWithCallback(
      const std::vector<std::string>& client_names,
      const base::Callback<void(const std::vector<ScopedMessagePipeHandle>*)>&
          callback);

  template <typename CallbackType>
  void RunChildren(const std::vector<std::string>& client_names,
                   const CallbackType& callback) {
    RunChildrenWithCallback(
        client_names,
        BindPipesHandler(
            [callback](const std::vector<ScopedMessagePipeHandle>* mps) {
              // reinterpret_cast is safe, as scoped handles have no overhead.
              callback(reinterpret_cast<const MojoHandle*>(mps->data())); }));
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

  // Reads a string from the pipe, expecting to read no handles.
  // Returns the string.
  static std::string ReadString(MojoHandle mp);

  // Writes |message| to |in| and expects to read it back from |out|.
  static void VerifyTransmission(MojoHandle in,
                                 MojoHandle out,
                                 const std::string& message);

  // Writes |message| to |mp| and expects to read it back from the same handle.
  static void VerifyEcho(MojoHandle mp, const std::string& message);

 private:
  DISALLOW_COPY_AND_ASSIGN(MultiprocessTestBase);
};

#define CREATE_PIPE(a, b) MojoHandle a, b; CreatePipe(&a, &b);

#define RUN_WITH_CHILD(client_name) RunChild(#client_name,

#define ON_PIPE(handlevar) [](MojoHandle handlevar) {

#define END_CHILD() });

#define EXPAND_CLIENT_NAMES(client_names...)  client_names

#define RUN_WITH_CHILDREN(client_names...)                       \
    {                                                            \
       std::vector<std::string> client_names_ {                  \
            EXPAND_CLIENT_NAMES(client_names) };                 \
       RunChildren(client_names_,

#define ON_PIPES(handlearray) [](const MojoHandle* handlearray) {

#define END_CHILDREN() });}

// Use this to declare the child process's "main()" function for tests using
// MultiprocessTestBase and MultiprocessTestHelper. It returns an |int|, which
// will be the process's exit code (but see the comment about
// WaitForChildShutdown()).
//
// The function is defined as a static member of a subclass of
// MultiprocessTestBase so code within it has access to that class's static
// static helpers.
#define DEFINE_TEST_CLIENT_WITH_PIPE(test_child_name, pipe_name)            \
  class test_child_name##_MainFixture : public test::MultiprocessTestBase { \
   public:                                                                  \
    static int AsyncMain(MojoHandle);                                       \
    static int AsyncMainScoped(ScopedMessagePipeHandle mp) {                \
      return AsyncMain(mp.get().value());                                   \
    }                                                                       \
  };                                                                        \
  MULTIPROCESS_TEST_MAIN_WITH_SETUP(                                        \
      test_child_name##TestChildMain,                                       \
      test::MultiprocessTestHelper::ChildSetup) {                           \
        return test::MultiprocessTestHelper::RunChildAsyncMain(             \
            base::Bind(&test_child_name##_MainFixture::AsyncMainScoped));   \
      }                                                                     \
      int test_child_name##_MainFixture::AsyncMain(MojoHandle pipe_name)


}  // namespace test
}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_MULTIPROCESS_TEST_BASE_H_
