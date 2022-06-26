/* Rexx file library, a set of functions to help some guy in a hurry
 * we'll see where it goes
 * 
 * The idea is to have some special kinds of files using an interface much
 * like linein and lineout. This could actually be done using system-dependent
 * parameters to the stream() function.
 *
 * The functions define here are:
 *   value = filelinein(command[,[line][,[count]][,stream]])
 *   count = filelineout(command[,[string][,line]])
 *   value = filecharin(command[,[line][,[count]][,stream]])
 *   count = filecharout(command[,[string][,line]])
 *   handle = fileopen(command,how)
 *   rc = fileclose(command[,stream])
 *   count = filelines(command[, stream])
 *
 * Copyright 1999, Patrick TJ McPhee
 * $Header: C:/ptjm/rexx/rexxfile/RCS/rexxfile.c 1.8 2002/04/20 17:36:00 pmcphee Exp $
 */

#  define INCL_DOSPROCESS
#include "rxproto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifdef _WIN32
# include <windows.h>
# include <io.h>
# define strcasecmp _stricmp
# define pid_t HANDLE
#else
# include <unistd.h>
# include <fcntl.h>
# include <strings.h>
# ifdef __EMX__
#  include <os2.h>
#  include <process.h>
static int popen3(const char * const cmd, FILE ** inh, FILE ** outh, FILE **errh);
void mypclose(int pid, int * status);
# endif
#endif

typedef struct fdesc_s {
   pid_t prochandle;
   FILE * inh,  * outh, * errh;
   const char * name;
   const char * command;
} fdesc_t;


static struct fdescarray_s {
   int count, alloc;
   fdesc_t * files;
} ftable;

/* linear search for the file. This is on the theory that there won't be
 * a lot of these things open at once, so there's no point keeping a
 * sorted list */
fdesc_t * file_find(const char * const s)
{
   register int i;

   for (i = 0; i < ftable.count; i++) {
      if (ftable.files[i].name && !strcmp(s, ftable.files[i].name))
         break;
   }

   if (i < ftable.count)
      return ftable.files+i;
   else
      return NULL;
}


void file_delete(const char * const s, char **oldcmd, int * status)
{
   fdesc_t *fd = file_find(s);

   if (oldcmd)
      *oldcmd = NULL;

   if (fd) {
      free(fd->name);
      if (fd->name != fd->command) {
         if (oldcmd)
            *oldcmd = fd->command;
         else
            free(fd->command);
      }

      fd->name = fd->command = NULL;

      if ((fd - ftable.files) == (ftable.count-1))
         ftable.count--;

      /* close all the handles and wait for the process to go away */
      if (fd->inh)
         fclose(fd->inh);
      if (fd->outh)
         fclose(fd->outh);
      if (fd->errh)
         fclose(fd->errh);

#ifndef _WIN32
# ifdef ONEWAY
      waitpid(fd->prochandle, status, 0);
# else
      mypclose(fd->prochandle, status);
# endif
#else
      WaitForSingleObject(fd->prochandle, INFINITE);
      { unsigned long lsta;
      GetExitCodeProcess(fd->prochandle, &lsta);
      CloseHandle(fd->prochandle);
      *status = lsta;
      }
#endif
   }
}



/* given a command string, parse out the white-space delimited arguments,
 * allowing quotation using ', ", and \ according to the Bourne shell
 * conventions. */
char ** parse_cmd(const char * const cmd)
{
   char **argv;
   register int i, j, argc;
   enum { skipping, arging, stringenting, stringing } state, prevstate;
   
   for (i = argc = 0, state = skipping; cmd[i]; i++) {
      switch (cmd[i]) {
         case ' ':
         case '\t':
         case '\n':
         case '\r':
         case '\f':
            if (state == arging) argc++, state = skipping;
            break;
         case '\\':
            if (state != stringenting && cmd[i+1]) i++;
            if (state == skipping) state = arging;
            break;
         case '"':
            if (state == stringing) state = prevstate;
            else if (state != stringenting) {
               state = stringing;
               prevstate = arging;
            }
            break;
         case '\'':
            if (state == stringenting) state = prevstate;
            else if (state != stringing) {
               state = stringenting;
               prevstate = arging;
            }
            break;
         default:
            if (state == skipping)
               state = arging;
      }
   }
   if (state != skipping)
      argc++;

   /* allocate a single block of memory for the argv array */
   argv = malloc((argc+1)*sizeof(*argv)+i+1);
   argv[0] = (char *)(argv+(argc+1));

   /* now do it all again, this time copying stuff over */
   for (i = j = argc = 0, state = skipping; cmd[i]; i++) {
      switch (cmd[i]) {
         case ' ':
         case '\t':
         case '\n':
         case '\r':
         case '\f':
            if (state == arging) {
               argv[argc][j] = 0;
               argv[argc+1] = argv[argc]+j+1;
               argc++;
               j = 0;
               state = skipping;
            }
            else if (state != skipping) {
               argv[argc][j++] = cmd[i];
            }
            break;
         case '\\':
            if (state != stringenting && cmd[i+1]) i++;
            if (state == skipping) state = arging;
            argv[argc][j++] = cmd[i];
            break;
         case '"':
            if (state == stringing) state = prevstate;
            else if (state != stringenting) {
               state = stringing;
               prevstate = arging;
            }
            else {
               argv[argc][j++] = cmd[i];
            }
            break;
         case '\'':
            if (state == stringenting) state = prevstate;
            else if (state != stringing) {
               state = stringenting;
               prevstate = arging;
            }
            else {
               argv[argc][j++] = cmd[i];
            }
            break;
         default:
            argv[argc][j++] = cmd[i];
            if (state == skipping)
               state = arging;
      }
   }
   if (state != skipping)
      argv[argc++][j++] = 0;

   argv[argc] = NULL;

   return argv;
}

#ifdef _WIN32
int start_command(const char * const cmd, fdesc_t * fd)
{
   int stdinpipe[2], stdoutpipe[2], stderrpipe[2];
   HANDLE me = GetCurrentProcess(), myfile;
   STARTUPINFO si;
   PROCESS_INFORMATION pi;
   SECURITY_ATTRIBUTES sa;
   int rc = 0;
   
   sa.nLength = sizeof(sa);
   sa.lpSecurityDescriptor = NULL;
   sa.bInheritHandle = FALSE;

   /* create pipes for the new process's standard handles */
   if (_pipe(stdinpipe, 0, 0) ||
       _pipe(stdoutpipe, 0, 0) ||
       _pipe(stderrpipe, 0, 0))
      return 0;

   memset(&si, 0, sizeof(si));
   si.cb = sizeof(si);
   si.dwFlags = STARTF_USESTDHANDLES ;

   /* make our end non-inheritable */
   DuplicateHandle(me, _get_osfhandle(stdinpipe[1]), me, &myfile,
    0, FALSE, DUPLICATE_SAME_ACCESS);
   close(stdinpipe[1]);
   stdinpipe[1] = _open_osfhandle((unsigned long)myfile, 0);
   DuplicateHandle(me, _get_osfhandle(stdoutpipe[0]), me, &myfile,
    0, FALSE, DUPLICATE_SAME_ACCESS);
   close(stdoutpipe[0]);
   stdoutpipe[0] = _open_osfhandle((unsigned long)myfile, 0);
   DuplicateHandle(me, _get_osfhandle(stderrpipe[0]), me, &myfile,
    0, FALSE, DUPLICATE_SAME_ACCESS);
   close(stderrpipe[0]);
   stderrpipe[0] = _open_osfhandle((unsigned long)myfile, 0);

   si.hStdInput = (HANDLE)_get_osfhandle(stdinpipe[0]);
   si.hStdOutput = (HANDLE)_get_osfhandle(stdoutpipe[1]);
   si.hStdError = (HANDLE)_get_osfhandle(stderrpipe[1]);

   if (!CreateProcess(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
      unsigned long result;
      close(stdinpipe[0]);
      close(stdinpipe[1]);
      close(stdoutpipe[0]);
      close(stdoutpipe[1]);
      close(stderrpipe[0]);
      close(stderrpipe[1]);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      rc = 0;
   }
   else {
      fd->inh = fdopen(stdinpipe[1], "w");
      fd->outh = fdopen(stdoutpipe[0], "r");
      fd->errh = fdopen(stderrpipe[0], "r");
      fd->prochandle = pi.hProcess;
      CloseHandle(pi.hThread);
      close(stdinpipe[0]);
      close(stdoutpipe[1]);
      close(stderrpipe[1]);
      rc = 1;
   }

/* 
   CloseHandle(&si.hStdInput);
   CloseHandle(&si.hStdOutput);
   CloseHandle(&si.hStdError);
*/
   return rc;
}
#elif defined(__EMX__)
int start_command(const char * const cmd, fdesc_t * fd)
{
   register int i;

   fd->prochandle = popen3(cmd, &fd->inh, &fd->outh, &fd->errh);
   return fd->prochandle > 0;
}
#else
int start_command(const char * const cmd, fdesc_t * fd)
{
   int stdinpipe[2], stdoutpipe[2], stderrpipe[2], oldhandles[3];
   char * cmdbuf, **argv;
   register int i;

   argv = parse_cmd(cmd);

   if (argv == NULL)
      return 0;

   /* create pipes for the new process's standard handles */
   if (pipe(stdinpipe) ||
       pipe(stdoutpipe) ||
       pipe(stderrpipe))
      return 0;

   /* now create the other process */
   if (!(fd->prochandle = fork())) {

      /* set up the handles for the new process */
      dup2(stdinpipe[0], 0);
      dup2(stdoutpipe[1], 1);
      dup2(stderrpipe[1], 2);

      /* get rid of all the pipe fds in the child process */
      close(stdinpipe[0]);
      close(stdinpipe[1]);
      close(stdoutpipe[0]);
      close(stdoutpipe[1]);
      close(stderrpipe[0]);
      close(stderrpipe[1]);

      execvp(argv[0], argv);
      _exit(1);
   }
   
   free(argv);

   /* get rid of the other side's ends */
   close(stdinpipe[0]);
   close(stdoutpipe[1]);
   close(stderrpipe[1]);

   if (fd->prochandle > 0) {
      /* and make stdio file things with them */
      fd->inh = fdopen(stdinpipe[1], "wb");
      fd->outh = fdopen(stdoutpipe[0], "rb");
      fd->errh = fdopen(stderrpipe[0], "rb");
   }
   else {
      close(stdinpipe[1]);
      close(stdoutpipe[0]);
      close(stderrpipe[0]);
   }

   return fd->prochandle > 0;
}
#endif



fdesc_t * file_add(const char * const name, const char * const cmd)
{
   register int i, nullval = -1;

   /* look for a duplicate name, and for the first unused slot */
   for (i = 0; i < ftable.count; i++) {
      if (ftable.files[i].name && !strcmp(name, ftable.files[i].name))
         return NULL;
      if (!ftable.files[i].name)
         nullval = i;
   }

   if (nullval > -1) {
      i = nullval;
   }

   /* do we need more room? */
   if (i >= ftable.alloc) {
      ftable.alloc = i+10;
      ftable.files = realloc(ftable.files, sizeof(*ftable.files)*ftable.alloc);
      if (ftable.files == NULL)
         return NULL;
   }

   if (start_command(cmd, ftable.files+i)) {
      if (i >= ftable.count) {
         ftable.count = i+1;
      }

      ftable.files[i].name = strdup(name);
      if (name == cmd) {
         ftable.files[i].command = ftable.files[i].name;
      }
      else {
         ftable.files[i].command = strdup(name);
      }

      return ftable.files+i;
   }
   else {
      return NULL;
   }
}




/* value = filelinein(command[,[line][,[count]][,stdout/stderr]])
 *  Read at most one line from a process running command. If the command is not
 *  already running, it starts the process and reads from the start of the output
 *  stream. Otherwise, it reads from the current position in the output stream.
 *  If there is no output available, linein waits for some to become available.
 *  If the line argument is specified, the command is ended and re-started.
 *  If the count argument is specified, it should be 0 or 1, and it specifies
 *  the number of lines to read. 0 means to not bother reading, and you'd use
 *  it for the side-effect of restarting the pipe.
 *  If the fourth argument evaluates to 'error', then reads from the standard error
 *  stream. Otherwise, it reads from the stdandard output stream.
 *  command can also be a handle returned by fileopen.
 *  On success, returns 0. On failure, returns 1.
 */
rxfunc(filelinein)
{
    char * cmd, *line = NULL, *scount = NULL, *stdwhat = NULL, *oldcmd;
    fdesc_t *fd;
    FILE * inf;

    checkparam(1,4);
    rxstrdup(cmd, argv[0]);

    if (argc > 1 && argv[1].strlength)
       rxstrdup(line, argv[1]);

    if (argc > 2 && argv[2].strlength)
       rxstrdup(scount, argv[2]);

    if (argc > 3 && argv[3].strlength)
       rxstrdup(stdwhat, argv[3]);

    if (line) {
       file_delete(cmd, &oldcmd, NULL);
       if (oldcmd) {
          fd = file_add(cmd, oldcmd);
          free(oldcmd);
       }
       else
          fd = file_add(cmd, cmd);
    }
    else {
       fd = file_find(cmd);
       if (!fd)
          fd = file_add(cmd, cmd);
    }

    /* no such command, no command running */
    if (!fd) {
       result->strlength = 0;
       return 0;
    }

    /* if count was 0, just return */
    if (scount && !atoi(scount)) {
       result->strlength = 0;
       return 0;
    }

    if (stdwhat && !strcasecmp(stdwhat, "error")) {
       inf = fd->errh;
    }
    else {
       inf = fd->outh;
    }

    /* oops -- we closed this! */
    if (!inf) {
       result->strlength = 0;
       return 0;
    }

    /* read the next line */
    if (!fgets(result->strptr, DEFAULTSTRINGSIZE, inf))
       result->strlength = 0;
    else {
       result->strlength = strlen(result->strptr) - 1;
       if (result->strptr[result->strlength] != '\n')
          result->strlength++;
    }

    return 0;
}

/* value = filecharin(command[,[offset][,[count]][,stdout/stderr]])
 *  Read count characters from a process running command. If the command is not
 *  already running, it starts the process and reads from the start of the output
 *  stream. Otherwise, it reads from the current position in the output stream.
 *  If count bytes are not available, charin waits for the process to write more
 *  data to its output stream.
 *  If the offset argument is specified, the command is ended and re-started.
 *  If the count argument is specified, it should be greater than or equal to 0,
 *  and it specifies the number of characters to read. 0 means to not bother reading,
 *  and you'd use it for the side-effect of restarting the pipe. The default is 1.
 *  If the fourth argument evaluates to 'error', then reads from the standard error
 *  stream. Otherwise, it reads from the stdandard output stream.
 *  command can also be a handle returned by fileopen.
 *  On success, returns 0. On failure, returns the number of bytes left to be read (or 1
 *  if count was 0).
 */
rxfunc(filecharin)
{
    char * cmd, *line = NULL, *scount = NULL, *stdwhat = NULL, *oldcmd;
    int count, br, rc;
    fdesc_t *fd;
    FILE * inf;

    checkparam(1,4);
    rxstrdup(cmd, argv[0]);

    if (argc > 1 && argv[1].strlength)
       rxstrdup(line, argv[1]);

    if (argc > 2 && argv[2].strlength)
       rxstrdup(scount, argv[2]);

    if (argc > 3 && argv[3].strlength)
       rxstrdup(stdwhat, argv[3]);

    if (line) {
       file_delete(cmd, &oldcmd, NULL);
       if (oldcmd) {
          fd = file_add(cmd, oldcmd);
          free(oldcmd);
       }
       else
          fd = file_add(cmd, cmd);
    }
    else {
       fd = file_find(cmd);
       if (!fd)
          fd = file_add(cmd, cmd);
    }

    /* no such command, no command running */
    if (!fd) {
       result->strlength = 0;
       return 0;
    }

    /* if count was 0, just return */
    if (scount) {
       count = atoi(scount);
    }
    else {
       count = 1;
    }

    if (!count) {
       result->strlength = 0;
       return 0;
    }

    if (stdwhat && !strcasecmp(stdwhat, "error")) {
       inf = fd->errh;
    }
    else {
       inf = fd->outh;
    }

    /* oops -- we closed this! */
    if (!inf) {
       result->strlength = 0;
       return 0;
    }

    /* read the next line */
    rxresize(result, count);

    for (br = 0, rc = fread(result->strptr+br, 1, count-br, inf);
         br < count && rc > 0;
         rc =  fread(result->strptr+br, 1, count-br, inf))
       br += rc;

    result->strlength = br;

    return 0;
}


/* count = filelineout(command[,[string][,line]])
 *  Write string to a process running command & terminate it with a new-line.
 * count = filecharout(command[,[string][,line]])
 *  Write string to a process running command.
 *  If the command is not already running, it starts the process.
 *  If the line argument is specified, the command is ended and re-started.
 *  On success, returns 0. On failure, returns 1.
 */
rxfunc(filelineout)
{
   char * cmd, *data, *oldcmd;
   int bw, rc, datal, restart;
   fdesc_t *fd;

   checkparam(1,3);
   rxstrdup(cmd, argv[0]);

   if (argc > 1 && argv[1].strlength) {
      data = argv[1].strptr;
      datal = argv[1].strlength;
   }
   else {
      datal = 0;
   }

   if (argc > 2 && argv[2].strlength)
      restart = 1;
   else
      restart = 0;

   if (restart) {
      file_delete(cmd, &oldcmd, NULL);
      if (oldcmd) {
         fd = file_add(cmd, oldcmd);
         free(oldcmd);
      }
      else
         fd = file_add(cmd, cmd);
   }
   else {
      fd = file_find(cmd);
      if (!fd)
         fd = file_add(cmd, cmd);
   }

   /* no such command, no command running, output handle closed */
   if (!fd || !fd->inh) {
      result_one();
      return 0;
   }

   if (datal)
      for (bw = 0, rc = fwrite(data+bw, 1, datal-bw, fd->inh);
           bw < datal && rc > 0;
           rc =  fwrite(data+bw, 1, datal-bw, fd->inh))
         bw += rc;

   if (rc >= 0) {
      if (!strcmp("FILELINEOUT", fname))
         fwrite("\n", 1, 1, fd->inh);
      fflush(fd->inh);
      result_zero();
   }
   else {
      result_one();
   }

   return 0;
}

/* handle = fileopen(command,how)
 * open the file or command or whatever, and return a handle. how must currently
 * be 'pipe'.
 * If the same command is already running, fileopen will open a second instance.
 * The only way to have two independent, eg, sort commands going simultaneously
 * is to open at least one of them with fileopen. Otherwise, there's no difference
 * between this and opening the command with filelinein */
rxfunc(fileopen)
{
   char * cmd, *how;
   static int nexthandle = 0;

   checkparam(2,2);
   rxstrdup(cmd, argv[0]);
   rxstrdup(how, argv[1]);

   if (strcasecmp(how, "pipe"))
      return BADARGS;

   result->strlength = sprintf(result->strptr, "%010d", ++nexthandle);

   if (!file_add(result->strptr, cmd))
      result_zero();

   return 0;
}

/* close a command which was opened either explicitly with fileopen or
 * implicitly with filelinein et al */
rxfunc(fileclose)
{
   char * cmd, *which;
   int status;
   fdesc_t *fd;

   checkparam(1,2);

   rxstrdup(cmd, argv[0]);
   fd = file_find(cmd);

   if (!fd) {
      memcpy(result->strptr, "-1", 2);
      result->strlength = 2;
      return 0;
   }

   if (argc > 1) {
      rxstrdup(which, argv[1]);
      if (!strcasecmp(which, "in") && fd->inh) {
         fclose(fd->inh);
         fd->inh = NULL;
         result_zero();
      }
      else if (!strcasecmp(which, "out") && fd->outh) {
         fclose(fd->outh);
         fd->outh = NULL;
         result_zero();
      }
      else if (!strcasecmp(which, "error") && fd->errh) {
         fclose(fd->errh);
         fd->errh = NULL;
         result_zero();
      }
      else
         result_one();
   }
   else {
      file_delete(cmd, NULL, &status);
      result->strlength = sprintf(result->strptr, "%d", status);
   }

   return 0;
}

/* feof() only works if an attempt has already been made to read past the
 * end of the stream, so if the other end of the pipe has been closed, but
 * we haven't got that far, feof() returns false, and we will end up trying to
 * read past the end. myfeof() always gets a character, so it's less
 * efficient but correct. Also, myfeof() allows fp to be null. */
static int myfeof(FILE * fp)
{
   int c;

   if (!fp || (c = fgetc(fp)) == EOF)
      return 1;
   else {
      ungetc(c, fp);
      return 0;
   }
}



/* return 1 if the command has more stuff to return, or 0 otherwise */
rxfunc(filelines)
{
   char * cmd, *which;
   fdesc_t *fd;
   FILE * inf;

   checkparam(1,2);

   rxstrdup(cmd, argv[0]);
   fd = file_find(cmd);

   if (fd) {
      if (argc > 1)
         rxstrdup(which, argv[1]);
      else
         which = NULL;

      if (which && !strcasecmp(which, "error"))
         inf = fd->errh;
      else
         inf = fd->outh;
   }

   if (fd && !myfeof(inf))
      result_one();
   else
      result_zero();

   return 0;
}


rxfunc(filedropfuncs);
rxfunc(fileloadfuncs);

static struct {
    char * name;
    APIRET (APIENTRY*funcptr)(PUCHAR fname, ULONG argc, PRXSTRING argv, PSZ pSomething, PRXSTRING result);
} funclist[] = {
    "FILELINEIN", filelinein,
    "FILELINEOUT", filelineout,
    "FILELINES", filelines,
    "FILECHARS", filelines,
    "FILECHARIN", filecharin,
    "FILECHAROUT", filelineout,
    "FILEOPEN", fileopen,
    "FILECLOSE", fileclose,
    "FILEDROPFUNCS", filedropfuncs,
    "FILELOADFUNCS", fileloadfuncs,
};

/* fileloadfuncs() */
rxfunc(fileloadfuncs)
{
    register int i;

    checkparam(0,0);

    for (i = 0; i < DIM(funclist); i++) {
	RexxRegisterFunctionExe(funclist[i].name, funclist[i].funcptr);
    }

    result_zero();

    return 0;
}

/* filedropfuncs() */
rxfunc(filedropfuncs)
{
    register int i;
    checkparam(0,0);

    for (i = 0; i < DIM(funclist); i++) {
	RexxDeregisterFunction(funclist[i].name);
    }

    result_zero();
    return 0;
}

#ifdef __EMX__
/* popen3 executes cmd in the back-ground, returning a write pointer to
 * its stdin in *inh, and read pointers to its stdout and stderr in *outh
 * and *errh, respectively. If any of those pointers are null, the
 * corresponding file descriptor is left alone.
 * Note that it's easy to deadlock if a single-threaded application both
 * reads and writes to the same application.
 * Returns the process ID on success, and -1 on failure */
int popen3(const char * const cmd, FILE ** inh, FILE ** outh, FILE **errh)
{
    int inp[2], outp[2], errp[2], oldin, oldout, olderr, rc = 0;
    int rcc;
    
    if (inh) {
	pipe(inp);

	oldin = dup(0);
	close(0);
	dup(inp[0]);
	close(inp[0]);
	fcntl(inp[1], F_SETFD, FD_CLOEXEC);
	fcntl(oldin, F_SETFD, FD_CLOEXEC);
	*inh = fdopen(inp[1], "w");
    }

    if (outh) {
	pipe(outp);
	oldout = dup(1);
	close(1);
	dup(outp[1]);
	close(outp[1]);
	fcntl(outp[0], F_SETFD, FD_CLOEXEC);
	fcntl(oldout, F_SETFD, FD_CLOEXEC);
	*outh = fdopen(outp[0], "r");
    }

    if (errh) {
	pipe(errp);
	olderr = dup(2);
	close(2);
	dup(errp[1]);
	close(errp[1]);
	fcntl(errp[0], F_SETFD, FD_CLOEXEC);
	fcntl(olderr, F_SETFD, FD_CLOEXEC);
	*errh = fdopen(errp[0], "r");
    }

#ifdef ONEWAY
    rc = spawnlp(P_NOWAIT, "cmd.exe", "cmd.exe", "/c", cmd, NULL);
#else
    {
	int l = strlen(cmd);
	char errb[1024], * cmdbuf = malloc(l+1), *cp;
	RESULTCODES res;

	memcpy(cmdbuf, cmd, l);
	cmdbuf[l+1] = 0;
	cp = strchr(cmdbuf, ' ');
	if (cp)
	    *cp++ = 0;

	rc = DosExecPgm(errb, sizeof(errb), EXEC_ASYNCRESULT, cp, NULL, &res, cmdbuf);

	if (rc) {
	    rc = -1;
	}
	else {
	    rc = res.codeTerminate;
	}
    }

#endif

    if (inh) {
	close(0);
	dup(oldin);
	close(oldin);
    }

    if (outh) {
	close(1);
	dup(oldout);
	close(oldout);
    }

    if (errh) {
	close(2);
	dup(olderr);
	close(olderr);
    }

    if (rc < 0) {
	if (inh) fclose(*inh);
	if (outh) fclose(*outh);
	if (errh) fclose(*errh);
    }

    return rc;
}

void mypclose(int pid, int * status)
{
    RESULTCODES res;
    PID procid;

    DosWaitChild(DCWA_PROCESS, DCWW_WAIT, &res, &procid, pid);
    *status = res.codeResult;
}
#endif
