if not defined CC set CC=gcc
if not defined CXX set CXX=g++
if not defined WINDRES set WINDRES=windres
if not defined OPT set OPT=3

pushd "%~dp0"
del /f /q sta.exe sdd.exe sta_debug.exe sdd_debug.exe bootimgsize_winapi.o sta.o sdd.o
"%CXX%" -c -o bootimgsize_winapi.o bootimgsize_winapi.cpp -Wall -Wextra -Wno-implicit-fallthrough -Wno-maybe-uninitialized -O%OPT% || goto fail
"%WINDRES%" sdd.rc sdd.o || goto fail
"%WINDRES%" sta.rc sta.o || goto fail
"%CC%" -o sdd.exe bootimgsize_winapi.o sdd.o sdd.c -std=c99 -pedantic -mwindows -Wall -Wextra -O%OPT% -s || goto fail
"%CC%" -o sta.exe bootimgsize_winapi.o sta.o sta.c -std=c99 -pedantic -mwindows -Wall -Wextra -O%OPT% -s || goto fail
"%CC%" -o sdd_debug.exe bootimgsize_winapi.o sdd.o sdd.c -std=c99 -pedantic -DDEBUG -Wall -Wextra -O%OPT% -s || goto fail
"%CC%" -o sta_debug.exe bootimgsize_winapi.o sta.o sta.c -std=c99 -pedantic -DDEBUG -Wall -Wextra -O%OPT% -s || goto fail
popd
exit /b

:fail
popd
pause
exit /b 1