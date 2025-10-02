#!/bin/sh
[ -z "$CC" ] && CC="aarch64-w64-mingw32-clang"
[ -z "$CXX" ] && CXX="aarch64-w64-mingw32-clang++"
[ -z "$WINDRES" ] && WINDRES="llvm-windres"
[ -z "$OPT" ] && OPT="3"

cd "$(dirname "$0")"
rm sdd.o sta.o sdd.exe sta.exe sdd_debug.exe sta_debug.exe 2>/dev/null
"$CXX" -c -o bootimgsize_winapi.o bootimgsize_winapi.cpp -Wall -Wextra -Wno-implicit-fallthrough "-O$OPT" || { cd - && exit 1;}
"$WINDRES" sdd.rc sdd.o || { cd - && exit 1;}
"$WINDRES" sta.rc sta.o || { cd - && exit 1;}
"$CC" -o sdd.exe bootimgsize_winapi.o sdd.o sdd.c -std=c99 -pedantic -mwindows -Wall -Wextra "-O$OPT" -s || { cd - && exit 1;}
"$CC" -o sta.exe bootimgsize_winapi.o sta.o sta.c -std=c99 -pedantic -mwindows -Wall -Wextra "-O$OPT" -s || { cd - && exit 1;}
"$CC" -o sdd_debug.exe bootimgsize_winapi.o sdd.o sdd.c -std=c99 -pedantic -DDEBUG -Wall -Wextra "-O$OPT" -s || { cd - && exit 1;}
"$CC" -o sta_debug.exe bootimgsize_winapi.o sta.o sta.c -std=c99 -pedantic -DDEBUG -Wall -Wextra "-O$OPT" -s || { cd - && exit 1;}
cd -
