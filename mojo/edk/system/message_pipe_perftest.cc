// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "base/test/perf_time_logger.h"
#include "mojo/edk/system/multiprocess_test_base.h"
#include "mojo/public/c/system/functions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace edk {
namespace {

class PipesPerfTest : public test::MultiprocessTestBase {
 public:
  PipesPerfTest() : message_count_(0), message_size_(0) {}

  void SetUpMeasurement(int message_count, size_t message_size) {
    message_count_ = message_count;
    message_size_ = message_size;
    payload_ = std::string(message_size, '*');
    read_buffer_.resize(message_size);
  }

 protected:
  void Measure(MojoHandle mp) {
    VerifyEcho(mp, payload_);
    std::string test_name =
        base::StringPrintf("IPC_Perf_%dx_%u", message_count_,
                           static_cast<unsigned>(message_size_));
    base::PerfTimeLogger logger(test_name.c_str());
    for (int i = 0; i < message_count_; ++i) {
      WriteString(mp, payload_);
      ReadString(mp, read_buffer_.data(), read_buffer_.size());
    }
    logger.Done();
  }

 private:
  int message_count_;
  size_t message_size_;
  std::string payload_;
  std::vector<char> read_buffer_;
  scoped_ptr<base::PerfTimeLogger> perf_logger_;
};

// For each message received, sends a reply message with the same contents
// repeated twice, until the other end is closed or it receives a "!"
// (which it doesn't reply to.)
DEFINE_TEST_CLIENT_WITH_PIPE(PingPongClient, PipesPerfTest, h) {
  std::vector<char> read_buffer(1000000);
  for (;;) {
    CHECK_EQ(MojoWait(h, MOJO_HANDLE_SIGNAL_READABLE, MOJO_DEADLINE_INDEFINITE,
                      nullptr),
             MOJO_RESULT_OK);

    uint32_t message_size = 0;
    uint32_t num_handles = 0;
    CHECK_EQ(MojoReadMessage(h, nullptr, &message_size, nullptr, &num_handles,
                             MOJO_READ_MESSAGE_FLAG_NONE),
             MOJO_RESULT_RESOURCE_EXHAUSTED);
    CHECK_LT(message_size, read_buffer.size());
    CHECK_EQ(num_handles, 0u);

    CHECK_EQ(MojoReadMessage(h, read_buffer.data(), &message_size, nullptr,
                             &num_handles, MOJO_READ_MESSAGE_FLAG_NONE),
             MOJO_RESULT_OK);
    CHECK_LT(message_size, read_buffer.size());
    CHECK_EQ(num_handles, 0u);

    if (message_size == 1 && read_buffer[0] == '!')
      break;

    CHECK_EQ(MojoWriteMessage(h, read_buffer.data(), message_size, nullptr, 0,
                              MOJO_WRITE_MESSAGE_FLAG_NONE),
             MOJO_RESULT_OK);
  }
  return 0;
}

// Repeatedly sends messages as previous one got replied by the child.
// Waits for the child to close its end before quitting once specified
// number of messages has been sent.
TEST_F(PipesPerfTest, PingPong) {
  RUN_WITH_CHILD(PingPongClient)
  ON_PIPE(h)
    // This values are set to align with one at ipc_pertests.cc for comparison.
    const size_t kMsgSize[5] = {12, 144, 1728, 20736, 248832};
    const int kMessageCount[5] = {50000, 50000, 50000, 12000, 1000};

    for (size_t i = 0; i < 5; i++) {
      SetUpMeasurement(kMessageCount[i], kMsgSize[i]);
      Measure(h);
    }

    WriteString(h, "!");
  END_CHILD()
}

}  // namespace
}  // namespace edk
}  // namespace mojo
