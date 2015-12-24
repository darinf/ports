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
  // These helpers let tests call RunChild with lambdas for readability.
  template <typename FuncType>
  static void CallPipeHandler(FuncType f, ScopedMessagePipeHandle pipe) {
    f(std::move(pipe));
  }

  template <typename FuncType>
  static base::Callback<void(ScopedMessagePipeHandle)>
  BindPipeHandler(FuncType f) {
    return base::Bind(&CallPipeHandler<FuncType>, f);
  }

  // Runs a new child process using |client_name| for the child test client.
  // |callback| is invoked with a message pipe handle connected to
  // |test::MultiprocessTestHelper::child_message_pipe| in the child process.
  //
  // RunChild does not return until the child process exits.
  template <typename CallbackType>
  void RunChild(const std::string& client_name, CallbackType callback) {
    test::MultiprocessTestHelper helper_;
    helper_.StartChild(client_name, BindPipeHandler(callback));
    helper_.RunUntilQuit();
    EXPECT_EQ(0, helper_.WaitForChildShutdown());
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

}  // namespace test
}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_MULTIPROCESS_TEST_BASE_H_
