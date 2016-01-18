# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import filecmp
import os
import os.path
import shutil
import sys


# Applies the ports EDK overlay to a local chromium checkout.


# These are copied non-recursively. If the destination path doesn't exist,
# it's created. A source file is only copied if it differs from the destination.
DIRS_TO_COPY_ = [
  "mojo",
  "mojo/edk/embedder",
  "mojo/edk/system",
  "mojo/edk/system/ports",
  "mojo/edk/test",
  "mojo/runner/child",
  "mojo/runner/host",
]


def CopyDirWithOverwrite(src_path, dest_path):
  if not os.path.exists(dest_path):
    os.mkdir(dest_path)
  for entry in os.listdir(src_path):
    src_entry = os.path.join(src_path, entry)
    dest_entry = os.path.join(dest_path, entry)
    if os.path.isfile(src_entry) and (not os.path.isfile(dest_entry) or
                                      not filecmp.cmp(src_entry, dest_entry)):
      shutil.copyfile(src_entry, dest_entry)


def CopyPortsEDKOverlay(ports_root, src_root):
  for dir in DIRS_TO_COPY_:
    CopyDirWithOverwrite(os.path.join(ports_root, dir),
                         os.path.join(src_root, dir))


if __name__ == "__main__":
  if len(sys.argv) < 2:
    print "Please provide the location of your chromium src."
    exit(1)
  exit(CopyPortsEDKOverlay(os.path.dirname(sys.argv[0]), sys.argv[1]))
