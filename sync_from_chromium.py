# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import filecmp
import os
import os.path
import shutil
import sys


# Copies any files in the ports EDK repo from the chromium repo. Useful for
# syncing this repo against upstream changes.


DIRS_TO_COPY_ = [
  "chrome/test/base",
  "components/test",
  "content/browser/mojo",
  "content/child",
  "content/child/mojo",
  "content/common/mojo",
  "mojo",
  "mojo/edk/embedder",
  "mojo/edk/system",
  "mojo/edk/system/ports",
  "mojo/edk/test",
  "mojo/shell/runner/child",
  "mojo/shell/runner/host",
  "net/test",
]


def UpdateDir(src_path, dest_path):
  for entry in os.listdir(dest_path):
    src_entry = os.path.join(src_path, entry)
    dest_entry = os.path.join(dest_path, entry)
    if os.path.isfile(dest_entry):
      shutil.copyfile(src_entry, dest_entry)


def UpdatePortsEDKOverlay(ports_root, src_root):
  for dir in DIRS_TO_COPY_:
    UpdateDir(os.path.join(src_root, dir), os.path.join(ports_root, dir))


if __name__ == "__main__":
  if len(sys.argv) < 2:
    print "Please provide the location of your chromium src."
    exit(1)
  exit(UpdatePortsEDKOverlay(os.path.dirname(sys.argv[0]), sys.argv[1]))
