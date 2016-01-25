// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/metrics/statistics_recorder.h"
#include "base/test/launcher/unit_test_launcher.h"
#include "base/test/test_io_thread.h"
#include "build/build_config.h"
#include "crypto/nss_util.h"
#include "net/socket/client_socket_pool_base.h"
#include "net/socket/ssl_server_socket.h"
#include "net/spdy/spdy_session.h"
#include "net/test/net_test_suite.h"

#if defined(OS_ANDROID)
#include "base/android/jni_android.h"
#include "base/android/jni_registrar.h"
#include "base/test/test_file_util.h"
#include "base/test/test_ui_thread_android.h"
#include "net/android/dummy_spnego_authenticator.h"
#include "net/android/net_jni_registrar.h"
#endif

#if defined(USE_ICU_ALTERNATIVES_ON_ANDROID)
#include "url/android/url_jni_registrar.h"
#endif

#if !defined(OS_ANDROID) && !defined(OS_IOS)
#include "mojo/edk/embedder/embedder.h"
#endif

using net::internal::ClientSocketPoolBaseHelper;
using net::SpdySession;

int main(int argc, char** argv) {
  // Record histograms, so we can get histograms data in tests.
  base::StatisticsRecorder::Initialize();

#if defined(OS_ANDROID)
  const base::android::RegistrationMethod kNetTestRegisteredMethods[] = {
    {"DummySpnegoAuthenticator",
     net::android::DummySpnegoAuthenticator::RegisterJni},
    {"NetAndroid", net::android::RegisterJni},
    {"TestFileUtil", base::RegisterContentUriTestUtils},
    {"TestUiThreadAndroid", base::RegisterTestUiThreadAndroid},
#if defined(USE_ICU_ALTERNATIVES_ON_ANDROID)
    {"UrlAndroid", url::android::RegisterJni},
#endif
  };

  // Register JNI bindings for android. Doing it early as the test suite setup
  // may initiate a call to Java.
  base::android::RegisterNativeMethods(
      base::android::AttachCurrentThread(),
      kNetTestRegisteredMethods,
      arraysize(kNetTestRegisteredMethods));
#endif

  scoped_refptr<base::TaskRunner> io_task_runner;
  NetTestSuite test_suite(argc, argv);
  ClientSocketPoolBaseHelper::set_connect_backup_jobs_enabled(false);

  // TODO(use_chrome_edk): This flag will go away and be default behavior soon,
  // but we explicitly add it here for test coverage.
  base::CommandLine::ForCurrentProcess()->AppendSwitch("use-new-edk");

#if defined(OS_WIN) && !defined(USE_OPENSSL)
  // We want to be sure to init NSPR on the main thread.
  crypto::EnsureNSPRInit();
#endif

  // Enable support for SSL server sockets, which must be done while
  // single-threaded.
  net::EnableSSLServerSockets();

#if !defined(OS_ANDROID) && !defined(OS_IOS)
  base::TestIOThread test_io_thread(base::TestIOThread::kAutoStart);
  io_task_runner = test_io_thread.task_runner();
  mojo::edk::Init(io_task_runner);
#endif

  return base::LaunchUnitTests(
      argc, argv, base::Bind(&NetTestSuite::Run,
                             base::Unretained(&test_suite)));
}
