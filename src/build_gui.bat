@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Users\gkd2323c\Documents\Hanako\c-ext

set GMP_INC=C:\Users\gkd2323c\AppData\Roaming\Python\Python314\site-packages\gmpy2
set GMP_LIBDIR=C:\Users\gkd2323c\AppData\Roaming\Python\Python314\site-packages\gmpy2.libs

cl /O2 /MT /TP /I"%GMP_INC%" pi_gui.c /link user32.lib gdi32.lib comctl32.lib /LIBPATH:"%GMP_LIBDIR%" gmp.lib /OUT:pi_gui.exe
echo EXIT_CODE=%ERRORLEVEL%
