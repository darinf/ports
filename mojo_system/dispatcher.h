// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/macros.h"
#include "base/memory/ref_counted.h"

namespace mojo {
namespace edk {

class Dispatcher : public base::RefCountedThreadSafe<Dispatcher> {
 public:
  Dispatcher();

 private:
  friend class base::RefCountedThreadSafe<Dispatcher>;

  ~Dispatcher();

  DISALLOW_COPY_AND_ASSIGN(Dispatcher);
};

}  // namespace edk
}  // namespace mojo
