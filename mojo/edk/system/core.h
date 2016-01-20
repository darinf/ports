// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_CORE_H_
#define MOJO_EDK_SYSTEM_CORE_H_

#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "mojo/edk/system/dispatcher.h"
#include "mojo/edk/system/handle_signals_state.h"
#include "mojo/edk/system/handle_table.h"
#include "mojo/edk/system/mapping_table.h"
#include "mojo/edk/system/node_controller.h"
#include "mojo/edk/system/system_impl_export.h"
#include "mojo/public/c/system/buffer.h"
#include "mojo/public/c/system/data_pipe.h"
#include "mojo/public/c/system/message_pipe.h"
#include "mojo/public/c/system/types.h"
#include "mojo/public/cpp/system/message_pipe.h"

namespace mojo {
namespace edk {

// |Core| is an object that implements the Mojo system calls. All public methods
// are thread-safe.
class MOJO_SYSTEM_IMPL_EXPORT Core {
 public:
  explicit Core();
  virtual ~Core();

  // Called exactly once, shortly after construction, and before any other
  // methods are called on this object.
  void SetIOTaskRunner(scoped_refptr<base::TaskRunner> io_task_runner);

  // Retrieves the NodeController for the current process.
  NodeController* GetNodeController();

  scoped_refptr<Dispatcher> GetDispatcher(MojoHandle handle);

  // Called in the parent process any time a new child is launched.
  void AddChild(base::ProcessHandle process_handle,
                ScopedPlatformHandle platform_handle);

  // Called in a child process exactly once during early initialization.
  void InitChild(ScopedPlatformHandle platform_handle);

  // These each create message pipe endpoints connected to an endpoint in a
  // remote embedder. |platform_handle| is used as a channel to negotiate the
  // connection. This is only here to facilitate legacy embedder code. See
  // mojo::edk::CreateMessagePipe in mojo/edk/embedder/embedder.h.
  void CreateParentMessagePipe(
      ScopedPlatformHandle platform_handle,
      const base::Callback<void(ScopedMessagePipeHandle)>& callback);
  void CreateChildMessagePipe(
      ScopedPlatformHandle platform_handle,
      const base::Callback<void(ScopedMessagePipeHandle)>& callback);

  // Creates a message pipe endpoint associated with |token|, which a child
  // holding the token can later locate and connect to.
  void CreateParentMessagePipe(
      const std::string& token,
      const base::Callback<void(ScopedMessagePipeHandle)>& callback);

  // Creates a message pipe endpoint associated with |token|, which will be
  // passed to the parent in order to find an associated remote port and connect
  // to it.
  void CreateChildMessagePipe(
      const std::string& token,
      const base::Callback<void(ScopedMessagePipeHandle)>& callback);

  MojoHandle AddDispatcher(scoped_refptr<Dispatcher> dispatcher);

  // Adds new message pipe dispatchers for every port contained in |message|.
  // |handles| should be an array of size |message.num_ports|.
  bool AddDispatchersForReceivedPorts(const ports::Message& message,
                                      MojoHandle* handles);

  // Adds new dispatchers for non-message-pipe handles received in a message.
  // |dispatchers| and |handles| should be the same size.
  bool AddDispatchersFromTransit(
      const std::vector<Dispatcher::DispatcherInTransit>& dispatchers,
      MojoHandle* handles);

  MojoResult CreatePlatformHandleWrapper(ScopedPlatformHandle platform_handle,
                                         MojoHandle* wrapper_handle);

  MojoResult PassWrappedPlatformHandle(MojoHandle wrapper_handle,
                                       ScopedPlatformHandle* platform_handle);

  // Watches on the given handle for the given signals, calling |callback| when
  // a signal is satisfied or when all signals become unsatisfiable. |callback|
  // must satisfy stringent requirements -- see |Awakable::Awake()| in
  // awakable.h. In particular, it must not call any Mojo system functions.
  MojoResult AsyncWait(MojoHandle handle,
                       MojoHandleSignals signals,
                       const base::Callback<void(MojoResult)>& callback);

  // ---------------------------------------------------------------------------

  // The following methods are essentially implementations of the Mojo Core
  // functions of the Mojo API, with the C interface translated to C++ by
  // "mojo/edk/embedder/entrypoints.cc". The best way to understand the contract
  // of these methods is to look at the header files defining the corresponding
  // API functions, referenced below.

  // These methods correspond to the API functions defined in
  // "mojo/public/c/system/functions.h":
  MojoTimeTicks GetTimeTicksNow();
  MojoResult Close(MojoHandle handle);
  MojoResult Wait(MojoHandle handle,
                  MojoHandleSignals signals,
                  MojoDeadline deadline,
                  MojoHandleSignalsState* signals_state);
  MojoResult WaitMany(const MojoHandle* handles,
                      const MojoHandleSignals* signals,
                      uint32_t num_handles,
                      MojoDeadline deadline,
                      uint32_t* result_index,
                      MojoHandleSignalsState* signals_states);

  // These methods correspond to the API functions defined in
  // "mojo/public/c/system/wait_set.h":
  MojoResult CreateWaitSet(MojoHandle* wait_set_handle);
  MojoResult AddHandle(MojoHandle wait_set_handle,
                       MojoHandle handle,
                       MojoHandleSignals signals);
  MojoResult RemoveHandle(MojoHandle wait_set_handle,
                          MojoHandle handle);
  MojoResult GetReadyHandles(MojoHandle wait_set_handle,
                             uint32_t* count,
                             MojoHandle* handles,
                             MojoResult* results,
                             MojoHandleSignalsState* signals_states);

  // These methods correspond to the API functions defined in
  // "mojo/public/c/system/message_pipe.h":
  MojoResult CreateMessagePipe(
      const MojoCreateMessagePipeOptions* options,
      MojoHandle* message_pipe_handle0,
      MojoHandle* message_pipe_handle1);
  MojoResult WriteMessage(MojoHandle message_pipe_handle,
                          const void* bytes,
                          uint32_t num_bytes,
                          const MojoHandle* handles,
                          uint32_t num_handles,
                          MojoWriteMessageFlags flags);
  MojoResult ReadMessage(MojoHandle message_pipe_handle,
                         void* bytes,
                         uint32_t* num_bytes,
                         MojoHandle* handles,
                         uint32_t* num_handles,
                         MojoReadMessageFlags flags);

  // These methods correspond to the API functions defined in
  // "mojo/public/c/system/data_pipe.h":
  MojoResult CreateDataPipe(
      const MojoCreateDataPipeOptions* options,
      MojoHandle* data_pipe_producer_handle,
      MojoHandle* data_pipe_consumer_handle);
  MojoResult WriteData(MojoHandle data_pipe_producer_handle,
                       const void* elements,
                       uint32_t* num_bytes,
                       MojoWriteDataFlags flags);
  MojoResult BeginWriteData(MojoHandle data_pipe_producer_handle,
                            void** buffer,
                            uint32_t* buffer_num_bytes,
                            MojoWriteDataFlags flags);
  MojoResult EndWriteData(MojoHandle data_pipe_producer_handle,
                          uint32_t num_bytes_written);
  MojoResult ReadData(MojoHandle data_pipe_consumer_handle,
                      void* elements,
                      uint32_t* num_bytes,
                      MojoReadDataFlags flags);
  MojoResult BeginReadData(MojoHandle data_pipe_consumer_handle,
                           const void** buffer,
                           uint32_t* buffer_num_bytes,
                           MojoReadDataFlags flags);
  MojoResult EndReadData(MojoHandle data_pipe_consumer_handle,
                         uint32_t num_bytes_read);

  // These methods correspond to the API functions defined in
  // "mojo/public/c/system/buffer.h":
  MojoResult CreateSharedBuffer(
      const MojoCreateSharedBufferOptions* options,
      uint64_t num_bytes,
      MojoHandle* shared_buffer_handle);
  MojoResult DuplicateBufferHandle(
      MojoHandle buffer_handle,
      const MojoDuplicateBufferHandleOptions* options,
      MojoHandle* new_buffer_handle);
  MojoResult MapBuffer(MojoHandle buffer_handle,
                       uint64_t offset,
                       uint64_t num_bytes,
                       void** buffer,
                       MojoMapBufferFlags flags);
  MojoResult UnmapBuffer(void* buffer);

  void GetActiveHandlesForTest(std::vector<MojoHandle>* handles);

 private:
  MojoResult WaitManyInternal(const MojoHandle* handles,
                              const MojoHandleSignals* signals,
                              uint32_t num_handles,
                              MojoDeadline deadline,
                              uint32_t *result_index,
                              HandleSignalsState* signals_states);

  // Used to pass ownership of our NodeController over to the IO thread in the
  // event that we're torn down before said thread.
  static void PassNodeControllerToIOThread(
      scoped_ptr<NodeController> node_controller);

  // This is lazily initialized on first access. Always use GetNodeController()
  // to access it.
  scoped_ptr<NodeController> node_controller_;

  base::Lock handles_lock_;
  HandleTable handles_;

  base::Lock mapping_table_lock_;  // Protects |mapping_table_|.
  MappingTable mapping_table_;

  DISALLOW_COPY_AND_ASSIGN(Core);
};

}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_CORE_H_
