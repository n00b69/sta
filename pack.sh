#!/bin/sh
cd "$(dirname "$0")"
rm *.7z *.exe *.o *.bak sdd.conf sta.conf 2>/dev/null
7z a -mx9 sta2.7z *
cd -