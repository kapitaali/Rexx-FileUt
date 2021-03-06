# Rexx File Utilities (FileUt), version 1.0.1

FileUt is a set of functions which provide special kinds of file handling to rexx programs, roughly following the Rexx IO model.  It gives functions filelinein, filelineout, filecharin, and filecharout, which work essentially the same way as the standard linein, lineout, charin, and charout functions, except they read and write special kinds of files.

In version 1.0, the library provides access to the standard input and output streams of running processes using pipes.  The effect is much like the pipe operator in awk.  In the future, it will possibly include access to various file archives and files compressed with gzip. Note that three years after the initial release, I haven't felt the need to do any of this.

This is version 1.0.1. I will increment the third digit each time I do a bug-fix release. I will increment the second digit if I add new kinds of special files.

To compile the library using Microsoft C, type nmake -f Makefile.nt. 
To compile the library under FreeBSD, type make -f Makefile.bsd. 
To compile the library using the EMX compiler, type make -f Makefile.emx. 
To compile the library under another Unix system, you might try taking the FreeBSD makefile as a departure point. Good luck, and let me know how it goes.  This code has been built on FreeBSD, OS/2, NT, Solaris, HP, and AIX.

To install on NT, copy NT/rexxfile.dll to a directory in your path (e.g., the directory with regina.exe). 

To install on OS/2, copy OS2/rexxfile.dll to a directory in your LIBPATH (or to the directory with regina.exe, although the OS/2 version is not tested with regina).

To install on Unix systems, copy librexxfile.so (or whatever it's called on your system :) to some central location and put that central location in your LD_LIBRARY_PATH, LIBPATH, SHLIB_PATH, or whatever it's called on your system.  FreeBSD has a nice utility (ldconfig) to set standard library searches, which you might want to investigate.

# Author

Patrick TJ McPhee
ptjm@interlog.com

# Notice

I have added this repository for better availability, originally from http://home.interlog.com/~ptjm/
