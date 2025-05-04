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

################################################################################
# This script builds libpng with custom config for OSS-Fuzz write fuzzer.
# It disables STDIO and WARNING but keeps WRITE support enabled.
################################################################################

# Modify pnglibconf.dfa to disable STDIO and WARNING logging (keep WRITE enabled).
sed -e "s/option STDIO/option STDIO disabled/" \
    -e "s/option WARNING /option WARNING disabled/" \
    scripts/pnglibconf.dfa > scripts/pnglibconf.dfa.temp
mv scripts/pnglibconf.dfa.temp scripts/pnglibconf.dfa

# Regenerate configure scripts and build libpng library (without tools).
autoreconf -f -i
./configure --with-libpng-prefix=OSS_FUZZ_
make -j"$(nproc)" clean
make -j"$(nproc)" libpng16.la

# Build the write fuzzer binary.
$CXX $CXXFLAGS -std=c++11 -I. \
     "$SRC/libpng/contrib/oss-fuzz/libpng_write_fuzzer.cc" \
     -o "$OUT/libpng_write_fuzzer" \
     .libs/libpng16.a -lz -lFuzzingEngine

# Create seed corpus ZIP (avoid crashers and handle spaces).
find "$SRC/libpng" -name "*.png" ! -path "*crashers*" -print0 | \
  zip -q -@ -0 "$OUT/libpng_write_fuzzer_seed_corpus.zip" --names-stdin -0 -0 -0

# Copy dictionary and fuzzer options if present.
cp "$SRC/libpng/contrib/oss-fuzz/"*.dict "$SRC/libpng/contrib/oss-fuzz/"*.options "$OUT/" || true

