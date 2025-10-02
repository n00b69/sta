pushd "%~dp0"
del /f /q *.7z *.exe *.o *.bak sdd.conf sta.conf
"C:\Program Files\7-Zip\7z.exe" a -mx9 sta2.7z *
popd