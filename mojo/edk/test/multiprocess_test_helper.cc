// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/test/multiprocess_test_helper.h"

#include <sstream>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/process/kill.h"
#include "base/process/process_handle.h"
#include "base/run_loop.h"
#include "base/task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "crypto/random.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/system/ports/name.h"

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#endif

namespace mojo {
namespace edk {
namespace test {

// static
ScopedMessagePipeHandle MultiprocessTestHelper::child_message_pipe;

namespace {

std::string GenerateRandomToken() {
  ports::NodeName token;
  crypto::RandBytes(&token, sizeof(token));

  std::stringstream ss;
  ss << token;
  return ss.str();
}

void RunChildMain(const base::Callback<int(ScopedMessagePipeHandle)>& main,
                  int* result_addr,
                  const base::Closure& quit_closure) {
  *result_addr = main.Run(
      std::move(MultiprocessTestHelper::child_message_pipe));
  quit_closure.Run();
}

void AsyncMainRunner(const base::Callback<int(ScopedMessagePipeHandle)>& main,
                     scoped_refptr<base::TaskRunner> main_runner,
                     int* result_addr,
                     const base::Closure& quit_closure,
                     ScopedMessagePipeHandle message_pipe) {
  MultiprocessTestHelper::child_message_pipe = std::move(message_pipe);
  main_runner->PostTask(
      FROM_HERE,
      base::Bind(&RunChildMain, main, base::Unretained(result_addr),
                 quit_closure));
}

void RunChildHandler(
    const base::Callback<void(ScopedMessagePipeHandle)>& callback,
    ScopedMessagePipeHandle message_pipe) {
  callback.Run(std::move(message_pipe));
}

}  // namespace

const char kPortTokenSwitch[] = "port-token";
const char kBrokerHandleSwitch[] = "broker-handle";

MultiprocessTestHelper::MultiprocessTestHelper() {
  port_token_ = GenerateRandomToken();
  broker_platform_channel_pair_.reset(new PlatformChannelPair());
}

MultiprocessTestHelper::~MultiprocessTestHelper() {
  CHECK(!test_child_.IsValid());
}

void MultiprocessTestHelper::StartChild(
    const std::string& test_child_name,
    const base::Callback<void(ScopedMessagePipeHandle)>& parent_main) {
  StartChildWithExtraSwitch(
      test_child_name, std::string(), std::string(), parent_main);
}

void MultiprocessTestHelper::StartChildWithExtraSwitch(
    const std::string& test_child_name,
    const std::string& switch_string,
    const std::string& switch_value,
    const base::Callback<void(ScopedMessagePipeHandle)>& parent_main) {
  CHECK(!test_child_name.empty());
  CHECK(!test_child_.IsValid());

  std::string test_child_main = test_child_name + "TestChildMain";

  base::CommandLine command_line(
      base::GetMultiProcessTestChildBaseCommandLine());
  HandlePassingInformation handle_passing_info;
  std::string broker_handle = broker_platform_channel_pair_->
      PrepareToPassClientHandleToChildProcessAsString(&handle_passing_info);
  command_line.AppendSwitchASCII(kBrokerHandleSwitch, broker_handle);
  command_line.AppendSwitchASCII(kPortTokenSwitch, port_token_);

  if (!switch_string.empty()) {
    CHECK(!command_line.HasSwitch(switch_string));
    if (!switch_value.empty())
      command_line.AppendSwitchASCII(switch_string, switch_value);
    else
      command_line.AppendSwitch(switch_string);
  }

  base::LaunchOptions options;
#if defined(OS_POSIX)
  options.fds_to_remap = &handle_passing_info;
#elif defined(OS_WIN)
  options.start_hidden = true;
  if (base::win::GetVersion() >= base::win::VERSION_VISTA)
    options.handles_to_inherit = &handle_passing_info;
  else
    options.inherit_handles = true;
#else
#error "Not supported yet."
#endif

  test_child_ =
      base::SpawnMultiProcessTestChild(test_child_main, command_line, options);

  broker_platform_channel_pair_->ChildProcessLaunched();
  ChildProcessLaunched(test_child_.Handle(),
                       broker_platform_channel_pair_->PassServerHandle());

  // This callback won't be invoked until the child is started and the
  // message pipe to it has been established.
  CreateParentMessagePipe(
      port_token_,
      base::Bind(&MultiprocessTestHelper::OnMessagePipeCreated,
                 base::Unretained(this), base::ThreadTaskRunnerHandle::Get(),
                 parent_main));

  CHECK(test_child_.IsValid());
}

int MultiprocessTestHelper::WaitForChildShutdown() {
  CHECK(test_child_.IsValid());

  int rv = -1;
  CHECK(
      test_child_.WaitForExitWithTimeout(TestTimeouts::action_timeout(), &rv));
  test_child_.Close();
  return rv;
}

bool MultiprocessTestHelper::WaitForChildTestShutdown() {
  return WaitForChildShutdown() == 0;
}

// static
void MultiprocessTestHelper::ChildSetup() {
  CHECK(base::CommandLine::InitializedForCurrentProcess());
  std::string broker_handle_str =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          kBrokerHandleSwitch);
  ScopedPlatformHandle broker_handle =
      PlatformChannelPair::PassClientHandleFromParentProcessFromString(
          broker_handle_str);
  SetParentPipeHandle(std::move(broker_handle));
}

// static
int MultiprocessTestHelper::RunChildAsyncMain(
    const base::Callback<int(ScopedMessagePipeHandle)>& main) {
  std::string port_token =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          kPortTokenSwitch);

  base::MessageLoop child_message_loop;
  base::RunLoop run_loop;
  int result = 0;
  CreateChildMessagePipe(
      port_token,
      base::Bind(&AsyncMainRunner, main, child_message_loop.task_runner(),
                 &result, run_loop.QuitClosure()));
  run_loop.Run();
  return result;
}

void MultiprocessTestHelper::OnMessagePipeCreated(
    scoped_refptr<base::TaskRunner> parent_main_task_runner,
    const base::Callback<void(ScopedMessagePipeHandle)>& parent_main,
    ScopedMessagePipeHandle message_pipe) {
  parent_main_task_runner->PostTask(
      FROM_HERE,
      base::Bind(&RunChildHandler, parent_main, base::Passed(&message_pipe)));
}

}  // namespace test
}  // namespace edk
}  // namespace mojo
