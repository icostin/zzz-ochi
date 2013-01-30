@echo off
set CSRC=ochi.c
set deps=c41.lib hbs1clid.lib hbs1.lib acx1.lib
set copts=/nologo /W4 /wd4201
call %VS90COMNTOOLS%\vsvars32.bat
cl.exe /Ox %copts% /Iinclude /I..\c41\include /D%D%_DL_BUILD /DNDEBUG /Feochi.exe %CSRC% /link %deps% /subsystem:console
::ochi ochi.exe
echo %ERRORLEVEL%