#!/bin/sh
set -e

EXPAT_VERSION='2.1.0'
EXPAT="expat-$EXPAT_VERSION.tar.gz"
LIBUTF="libutf.tgz"

rm -rf expat libutf

curl -L "http://downloads.sourceforge.net/project/expat/expat/$EXPAT_VERSION/expat-$EXPAT_VERSION.tar.gz" -o "$EXPAT"
curl "http://swtch.com/plan9port/unix/libutf.tgz" -o "$LIBUTF"

md5sum -c dependencies.sources

tar -xzf "$EXPAT"
tar -xzf "$LIBUTF"

mv "expat-$EXPAT_VERSION" expat

(cd libutf && patch -p2 <../libutf.patch)

rm -f libutf.tgz expat-2.1.0.tar.gz
