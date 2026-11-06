#!/bin/bash
# Dump answers to every read request the ESP/app was seen sending, for diffing
# before/after a change in the machine menu. Usage: ecam_dump.sh <outfile>
set -eu
E="python3 -I $(dirname "$0")/ecam.py"
p=()
# a1 0f <id16> <n>: parameter blocks read by the ESP after (re)connect
for idn in 0000:02 0028:01 002a:02 0032:01 003a:01 0064:02 0069:02 006c:02 006f:01 0074:01 \
           0191:07 019a:02 0bb8:0a 0bc2:07 0bca:02 0bd1:01 0bd8:01 0be6:09 0bef:0a 0bf9:0a \
           0c03:02 0c05:04 0c09:09 59d8:02 59da:08 a7f8:01 a7fd:01 a802:03 a806:03; do
  p+=("a10f${idn%:*}${idn#*:}")
done
for b in $(seq 1 27); do p+=("$(printf 'b0f0%02x' "$b")"); done
for s in 0 1 2 3 4 5 6; do p+=("baf00$s"); done
for s in 1 2 3 4; do p+=("a8f00$s"); done
p+=(a3f0 bcf0 950f00d201 950f002d01 950f00c201 95f003ee01 e8f000)
$E raw "${p[@]}" > "$1"
echo "$(grep -c '^RX [^-]' "$1") answers / ${#p[@]} requests -> $1"
