// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/multiprocess_test_base.h"

#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "mojo/edk/system/handle_signals_state.h"
#include "mojo/public/c/system/functions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace edk {
namespace test {

namespace {

void StoreClientHandle(
    size_t* clients_started,
    std::vector<ScopedMessagePipeHandle>* client_handles,
    size_t this_client_index,
    const base::Closure& on_all_clients_started,
    ScopedMessagePipeHandle pipe) {
  (*clients_started)++;
  client_handles->at(this_client_index) = std::move(pipe);
  if (*clients_started == client_handles->size())
    on_all_clients_started.Run();
}

void RunCallbacksInSequence(const base::Closure& first,
                            const base::Closure& second) {
  first.Run();
  second.Run();
}

void RunPipesHandler(
    const std::vector<ScopedMessagePipeHandle>* handles,
    scoped_refptr<base::TaskRunner> task_runner,
    const base::Callback<void(const std::vector<ScopedMessagePipeHandle>*)>&
        callback,
    const base::Closure& quit_closure) {
  task_runner->PostTask(
      FROM_HERE,
      base::Bind(&RunCallbacksInSequence, base::Bind(callback, handles),
                 quit_closure));
}

}  // namespace

MultiprocessTestBase::MultiprocessTestBase() {}

MultiprocessTestBase::~MultiprocessTestBase() {}

void MultiprocessTestBase::RunChildWithCallback(
    const std::string& client_name,
    const base::Callback<void(ScopedMessagePipeHandle)>& callback) {
  test::MultiprocessTestHelper helper;
  base::MessageLoop message_loop;
  base::RunLoop run_loop;
  helper.StartChild(
      client_name,
      BindPipeHandler([callback, &run_loop](ScopedMessagePipeHandle mp) {
        callback.Run(std::move(mp));
        run_loop.Quit();
      }));
  run_loop.Run();
  EXPECT_EQ(0, helper.WaitForChildShutdown());
}

void MultiprocessTestBase::RunChildrenWithCallback(
    const std::vector<std::string>& client_names,
    const base::Callback<void(const std::vector<ScopedMessagePipeHandle>*)>&
        callback) {
  base::MessageLoop message_loop;
  base::RunLoop run_loop;

  size_t clients_started = 0;
  std::vector<ScopedMessagePipeHandle> handles(client_names.size());
  std::vector<test::MultiprocessTestHelper> helpers(client_names.size());
  base::Closure on_all_clients_started =
      base::Bind(&RunPipesHandler, base::Unretained(&handles),
                 message_loop.task_runner(), callback, run_loop.QuitClosure());

  for (size_t i = 0; i < client_names.size(); ++i) {
    auto handler = base::Bind(&StoreClientHandle,
        base::Unretained(&clients_started), base::Unretained(&handles), i,
        on_all_clients_started);
    helpers[i].StartChild(client_names[i], handler);
  }

  run_loop.Run();

  for (auto& helper : helpers)
    EXPECT_EQ(0, helper.WaitForChildShutdown());
}

// static
void MultiprocessTestBase::CreatePipe(MojoHandle *p0, MojoHandle* p1) {
  MojoCreateMessagePipe(nullptr, p0, p1);
  CHECK_NE(*p0, MOJO_HANDLE_INVALID);
  CHECK_NE(*p1, MOJO_HANDLE_INVALID);
}

// static
void MultiprocessTestBase::WriteStringWithHandles(
    MojoHandle mp,
    const std::string& message,
    const MojoHandle *handles,
    uint32_t num_handles) {
  CHECK_EQ(MojoWriteMessage(mp, message.data(),
                            static_cast<uint32_t>(message.size()),
                            handles, num_handles, MOJO_WRITE_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
}

// static
void MultiprocessTestBase::WriteString(MojoHandle mp,
                                       const std::string& message) {
  WriteStringWithHandles(mp, message, nullptr, 0);
}

// static
std::string MultiprocessTestBase::ReadStringWithHandles(
    MojoHandle mp,
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
  CHECK_EQ(message_size, message.size());
  CHECK_EQ(num_handles, expected_num_handles);

  return message;
}

// static
std::string MultiprocessTestBase::ReadStringWithOptionalHandle(
    MojoHandle mp,
    MojoHandle* handle) {
  CHECK_EQ(MojoWait(mp, MOJO_HANDLE_SIGNAL_READABLE, MOJO_DEADLINE_INDEFINITE,
                    nullptr),
           MOJO_RESULT_OK);

  uint32_t message_size = 0;
  uint32_t num_handles = 0;
  CHECK_EQ(MojoReadMessage(mp, nullptr, &message_size, nullptr, &num_handles,
                           MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_RESOURCE_EXHAUSTED);
  CHECK(num_handles == 0 || num_handles == 1);

  CHECK(handle);

  std::string message(message_size, 'x');
  CHECK_EQ(MojoReadMessage(mp, &message[0], &message_size, handle,
                           &num_handles, MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  CHECK_EQ(message_size, message.size());
  CHECK(num_handles == 0 || num_handles == 1);

  if (num_handles)
    CHECK_NE(*handle, MOJO_HANDLE_INVALID);
  else
    *handle = MOJO_HANDLE_INVALID;

  return message;
}

// static
std::string MultiprocessTestBase::ReadString(MojoHandle mp) {
  return ReadStringWithHandles(mp, nullptr, 0);
}

// static
void MultiprocessTestBase::ReadString(MojoHandle mp,
                                      char* data,
                                      size_t num_bytes) {
  CHECK_EQ(MojoWait(mp, MOJO_HANDLE_SIGNAL_READABLE, MOJO_DEADLINE_INDEFINITE,
                    nullptr),
           MOJO_RESULT_OK);

  uint32_t message_size = 0;
  uint32_t num_handles = 0;
  CHECK_EQ(MojoReadMessage(mp, nullptr, &message_size, nullptr, &num_handles,
                           MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_RESOURCE_EXHAUSTED);
  CHECK_EQ(num_handles, 0u);
  CHECK_EQ(message_size, num_bytes);

  CHECK_EQ(MojoReadMessage(mp, data, &message_size, nullptr, &num_handles,
                           MOJO_READ_MESSAGE_FLAG_NONE),
           MOJO_RESULT_OK);
  CHECK_EQ(num_handles, 0u);
  CHECK_EQ(message_size, num_bytes);
}

// static
void MultiprocessTestBase::VerifyTransmission(MojoHandle source,
                                              MojoHandle dest,
                                              const std::string& message) {
  WriteString(source, message);

  // We don't use EXPECT_EQ; failures on really long messages make life hard.
  EXPECT_TRUE(message == ReadString(dest));
}

// static
void MultiprocessTestBase::VerifyEcho(MojoHandle mp,
                                      const std::string& message) {
  VerifyTransmission(mp, mp, message);
}

}  // namespace test
}  // namespace edk
}  // namespace mojo
