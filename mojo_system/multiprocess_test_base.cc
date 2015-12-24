// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/multiprocess_test_base.h"

#include "mojo/edk/system/handle_signals_state.h"
#include "mojo/public/c/system/functions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace edk {
namespace test {

MultiprocessTestBase::MultiprocessTestBase() {}

MultiprocessTestBase::~MultiprocessTestBase() {}

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
  CHECK_EQ(MojoWait(mp, MOJO_HANDLE_SIGNAL_WRITABLE, MOJO_DEADLINE_INDEFINITE,
                    nullptr),
           MOJO_RESULT_OK);

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
  CHECK_EQ(expected_num_handles, num_handles);

  return message;
}

// static
std::string MultiprocessTestBase::ReadString(MojoHandle mp) {
  return ReadStringWithHandles(mp, nullptr, 0);
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
