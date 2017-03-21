 ZDoom 2.1.4 CLASSIC, a fork of ZDoom 2.1.4 (https://github.com/rheit/zdoom) compiled for 486 machines.
Includes a new quad horizontally and double vertically low detail mode from ZDOOM LE as well as pixel doubled and
quadrupled lowres directdraw modes from ZDoom 2.3.

 Compiles with CodeBlocks 16.01, TDM-GCC 4.6.1 and NASM 0.99.06. You'll need the following libraries:
dx80_mgw.zip and fmodapi375win.zip.
 A project for CodeBlocks is included, however version 16.01 is very buggy and you'd need to edit the project
 manually. You could try later versions or 12.11. Alternatively you can use the original included makefiles.
 
 Some notes:
 - Compiling with TDM-GCC 4.9.2 the exe would require Windows 98 to run and the compiler is much slower.
 GCC 3.4 (MinGW 3.4.2) also works and it's even faster.
 - Precompiled FLAC and Zlib libraries are also included.
 
 You can follow the following guide:
 https://zdoom.org/wiki/Compile_ZDoom_on_Windows
 