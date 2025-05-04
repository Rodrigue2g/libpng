#!/bin/bash -eu
# Copyright 2017-2018 Glenn Randers-Pehrson
# Copyright 2016 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################

# Disable logging via library build configuration control.
cat scripts/pnglibconf.dfa | \
  sed -e "s/option STDIO/option STDIO disabled/" \
      -e "s/option WARNING /option WARNING disabled/" \
      -e "s/option WRITE enables WRITE_INT_FUNCTIONS/option WRITE disabled/" \
> scripts/pnglibconf.dfa.temp
mv scripts/pnglibconf.dfa.temp scripts/pnglibconf.dfa

# Skip autoreconf and use direct configure script
# We'll manually modify the configure script
./configure --with-libpng-prefix=OSS_FUZZ_ || {
  # If configure fails due to the RISC-V error, apply fix and retry
  if [ -f "configure" ]; then
    # Fix the syntax error in the configure script
    sed -i 's/riscv\*)/riscv\*-\*)/' configure
    # Try configure again with the fix
    ./configure --with-libpng-prefix=OSS_FUZZ_
  fi
}

# Build the libpng library
make -j$(nproc) clean
make -j$(nproc) libpng16.la

# build libpng_write_fuzzer.
$CXX $CXXFLAGS -std=c++11 -I. \
     $SRC/libpng/contrib/oss-fuzz/libpng_write_fuzzer.cc \
     -o $OUT/libpng_write_fuzzer \
     -lFuzzingEngine .libs/libpng16.a -lz

# add seed corpus.
find $SRC/libpng -name "*.png" | grep -v crashers | \
     xargs zip $OUT/libpng_write_fuzzer_seed_corpus.zip
cp $SRC/libpng/contrib/oss-fuzz/*.dict \
     $SRC/libpng/contrib/oss-fuzz/*.options $OUT/
