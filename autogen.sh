#!/bin/sh
# Sinh lai he thong build tu ma nguon.
#
# Repo chi commit ma nguon; configure, Makefile.in, aclocal.m4... deu la file
# sinh ra nen nam trong .gitignore. Chay script nay sau khi clone:
#
#     ./autogen.sh
#     ./configure
#     make

set -e

command -v autoreconf >/dev/null 2>&1 || {
    echo "Thieu autoreconf. Cai: sudo apt install autoconf automake libtool"
    exit 1
}

autoreconf -i "$@"

echo ""
echo "Xong. Tiep theo:"
echo "    ./configure && make && make check"
