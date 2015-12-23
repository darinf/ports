// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_DISPATCHER_H_
#define PORTS_MOJO_SYSTEM_DISPATCHER_H_

#include <stddef.h>
#include <stdint.h>

#include <ostream>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "mojo/edk/system/handle_signals_state.h"
#include "mojo/edk/system/system_impl_export.h"
#include "mojo/public/c/system/message_pipe.h"
#include "mojo/public/c/system/types.h"
#include "mojo/public/cpp/system/macros.h"
#include "ports/include/ports.h"

namespace mojo {
namespace edk {

class Awakable;

// A |Dispatcher| implements Mojo primitives that are "attached" to a particular
// handle. This includes most (all?) primitives except for |MojoWait...()|. This
// object is thread-safe, with its state being protected by a single lock
// |lock_|, which is also made available to implementation subclasses (via the
// |lock()| method).
class MOJO_SYSTEM_IMPL_EXPORT Dispatcher
    : public base::RefCountedThreadSafe<Dispatcher> {
 public:
  struct DispatcherInTransit {
    DispatcherInTransit();
    ~DispatcherInTransit();

    scoped_refptr<Dispatcher> dispatcher;
    MojoHandle local_handle;
  };

  enum class Type {
    UNKNOWN = 0,
    MESSAGE_PIPE,
    DATA_PIPE_PRODUCER,
    DATA_PIPE_CONSUMER,
    SHARED_BUFFER,
    WAIT_SET,

    // "Private" types (not exposed via the public interface):
    PLATFORM_HANDLE = -1,

    // A special kind of message pipe which operates directly on a concrete
    // platform channel. It can transfer anything but message pipe handles.
    CHANNEL = -2,
  };
  virtual Type GetType() const = 0;

  // These methods implement the various primitives named |Mojo...()|. These
  // take |lock_| and handle races with |Close()|. Then they call out to
  // subclasses' |...ImplNoLock()| methods (still under |lock_|), which actually
  // implement the primitives.
  // NOTE(vtl): This puts a big lock around each dispatcher (i.e., handle), and
  // prevents the various |...ImplNoLock()|s from releasing the lock as soon as
  // possible. If this becomes an issue, we can rethink this.
  MojoResult Close();

  // |handles| contains all non-message-pipe handles to be transferred.
  // Message pipe handles must be translated to port names and specified via
  // |ports| and |num_ports|.
  MojoResult WriteMessage(const void* bytes,
                          uint32_t num_bytes,
                          const DispatcherInTransit* dispatchers,
                          uint32_t num_dispatchers,
                          MojoWriteMessageFlags flags);

  MojoResult ReadMessage(void* bytes,
                         uint32_t* num_bytes,
                         DispatcherInTransit* dispatchers,
                         uint32_t* num_dispatchers,
                         MojoReadMessageFlags flags);

  // Gets the current handle signals state. (The default implementation simply
  // returns a default-constructed |HandleSignalsState|, i.e., no signals
  // satisfied or satisfiable.) Note: The state is subject to change from other
  // threads.
  HandleSignalsState GetHandleSignalsState() const;

  // Adds an awakable to this dispatcher, which will be woken up when this
  // object changes state to satisfy |signals| with context |context|. It will
  // also be woken up when it becomes impossible for the object to ever satisfy
  // |signals| with a suitable error status.
  //
  // If |signals_state| is non-null, on *failure* |*signals_state| will be set
  // to the current handle signals state (on success, it is left untouched).
  //
  // Returns:
  //  - |MOJO_RESULT_OK| if the awakable was added;
  //  - |MOJO_RESULT_ALREADY_EXISTS| if |signals| is already satisfied;
  //  - |MOJO_RESULT_INVALID_ARGUMENT| if the dispatcher has been closed; and
  //  - |MOJO_RESULT_FAILED_PRECONDITION| if it is not (or no longer) possible
  //    that |signals| will ever be satisfied.
  MojoResult AddAwakable(Awakable* awakable,
                         MojoHandleSignals signals,
                         uintptr_t context,
                         HandleSignalsState* signals_state);
  // Removes an awakable from this dispatcher. (It is valid to call this
  // multiple times for the same |awakable| on the same object, so long as
  // |AddAwakable()| was called at most once.) If |signals_state| is non-null,
  // |*signals_state| will be set to the current handle signals state.
  void RemoveAwakable(Awakable* awakable, HandleSignalsState* signals_state);

  // Does whatever is necessary to begin transit of the dispatcher. Grabs and
  // holds the dispatcher's lock. This should return |true| if transit is OK,
  // or false if the underlying resource is deemed busy by the implementation.
  bool BeginTransit();

  // Does whatever is necessary to end transit of the dispatched. |canceled| is
  // true iff transit was canceled. Releases the dispatcher's lock.
  void EndTransit(bool canceled);

 protected:
  friend class base::RefCountedThreadSafe<Dispatcher>;

  Dispatcher();
  virtual ~Dispatcher();

  // These are to be overridden by subclasses (if necessary). They are called
  // exactly once -- first |CancelAllAwakablesNoLock()|, then
  // |CloseImplNoLock()|,
  // when the dispatcher is being closed. They are called under |lock_|.
  virtual void CancelAllAwakablesNoLock();
  virtual void CloseImplNoLock();

  // These are to be overridden by subclasses (if necessary). They are never
  // called after the dispatcher has been closed. They are called under |lock_|.
  // See the descriptions of the methods without the "ImplNoLock" for more
  // information.
  virtual MojoResult WriteMessageImplNoLock(
      const void* bytes,
      uint32_t num_bytes,
      const DispatcherInTransit* dispatchers,
      uint32_t num_dispatchers,
      MojoWriteMessageFlags flags);
  virtual MojoResult ReadMessageImplNoLock(void* bytes,
                                           uint32_t* num_bytes,
                                           DispatcherInTransit* dispatchers,
                                           uint32_t* num_dispatchers,
                                           MojoReadMessageFlags flags);
  virtual HandleSignalsState GetHandleSignalsStateImplNoLock() const;
  virtual MojoResult AddAwakableImplNoLock(Awakable* awakable,
                                           MojoHandleSignals signals,
                                           uintptr_t context,
                                           HandleSignalsState* signals_state);
  virtual void RemoveAwakableImplNoLock(Awakable* awakable,
                                        HandleSignalsState* signals_state);

  virtual bool BeginTransitImplNoLock();
  virtual void EndTransitImplNoLock(bool canceled);

  // This should be overridden to return true if/when there's an ongoing
  // operation (e.g., two-phase read/writes on data pipes) that should prevent a
  // handle from being sent over a message pipe (with status "busy").
  virtual bool IsBusyNoLock() const;

  // Available to subclasses. (Note: Returns a non-const reference, just like
  // |base::AutoLock|'s constructor takes a non-const reference.)
  base::Lock& lock() const { return lock_; }
  bool is_closed() const { return is_closed_; }

  // Closes the dispatcher. This must be done under lock, and unlike |Close()|,
  // the dispatcher must not be closed already. (This is the "equivalent" of
  // |CreateEquivalentDispatcherAndCloseNoLock()|, for situations where the
  // dispatcher must be disposed of instead of "transferred".)
  void CloseNoLock();

 private:
  // This protects the following members as well as any state added by
  // subclasses.
  mutable base::Lock lock_;
  bool is_closed_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(Dispatcher);
};

// So logging macros and |DCHECK_EQ()|, etc. work.
MOJO_SYSTEM_IMPL_EXPORT inline std::ostream& operator<<(std::ostream& out,
                                                        Dispatcher::Type type) {
  return out << static_cast<int>(type);
}

using DispatcherVector = std::vector<scoped_refptr<Dispatcher>>;

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_DISPATCHER_H_
