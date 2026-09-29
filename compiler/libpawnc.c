/*  LIBPAWNC.C
 *
 *  A "glue file" for building the Pawn compiler as a DLL or shared library.
 *
 *  Copyright (c) CompuPhase, 2000-2026
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may not
 *  use this file except in compliance with the License. You may obtain a copy
 *  of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 *  License for the specific language governing permissions and limitations
 *  under the License.
 *
 *  Version: $Id: libpawnc.c 7699 2026-09-25 21:36:04Z thiadmer $
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if !defined S_ISDIR
# define S_ISDIR(mode) (((mode) & _S_IFDIR) != 0)
#endif
#include "sc.h"

#if defined PAWNC_DLL

# include "../amx/dllmain.c"

# define MAX_ARGS   100
# if !defined UNUSED_PARAM
#   define UNUSED_PARAM(p) ((void)(p))
# endif

  static char *argv[MAX_ARGS];
  static int  argc;

  LPSTR dll_skipwhite(LPSTR ptr)
  {
    assert(ptr!=NULL);
    while (*ptr<=' ' && *ptr!='\0')
      ptr++;
    return ptr;
  }

# if defined __WIN32__ || defined _WIN32 || defined WIN32 || defined __NT__
  __declspec (dllexport)
  void CALLBACK Compile(HWND hwnd, HINSTANCE hinst, LPSTR lpCommandLine, int nCmdShow)
# else
  void FAR PASCAL __export Compile(HWND hwnd, HINSTANCE hinst, LPSTR lpCommandLine, int nCmdShow)
# endif
  {
    char RootPath[_MAX_PATH];
    LPSTR ptr;

    /* RUNDLL32 may have passed us a HWND and a HINSTANCE, but we can hardly
     * trust these. They may not contain values that we can use.
     */

    /* the root path in argv[0] */
    GetModuleFileName(hinstDLL, RootPath, sizeof RootPath);
    argv[argc++]=RootPath;

    /* all other options */
    assert(lpCommandLine!=NULL);
    ptr=dll_skipwhite(lpCommandLine);
    while (*ptr!='\0') {
      if (*ptr=='"') {
        argv[argc++]=ptr+1;
        while (*ptr!='"' && *ptr!='\0')
          ptr++;
      } else {
        argv[argc++]=ptr;
        while (*ptr>' ')
          ptr++;
      } /* if */
      if (*ptr!='\0')
        *ptr++='\0';
      ptr=dll_skipwhite(ptr);
    } /* while */
    pc_compile(argc,argv);
    UNUSED_PARAM(hwnd);
    UNUSED_PARAM(hinst);
    UNUSED_PARAM(nCmdShow);
  }

#else /* PAWNC_DLL */


#endif /* PAWNC_DLL */


/** pc_printf()
 *  Called for general purpose "console" output. This function prints general
 *  purpose messages; errors go through pc_error(). The function is modelled
 *  after printf().
 */
int pc_printf(const char *message,...)
{
  int ret;
  va_list argptr;

  va_start(argptr,message);
  ret=vprintf(message,argptr);
  va_end(argptr);

  return ret;
}

/** pc_error()
 *  Called for producing error output.
 *  \param number       The error number (as documented in the manual).
 *  \param message      [in] A string describing the error, with embedded '%d'
 *                      and '%s' tokens if applicable.
 *  \param filename     [in] The name of the file currently being parsed.
 *  \param firstline    The line number at which the expression started on which
 *                      the error was found, or -1 if there is no "starting
 *                      line".
 *  \param lastline     The line number at which the error was detected.
 *  \param argptr       a pointer to the first of a series of arguments (for
 *                      macro "va_arg").
 *  \return If the function returns 0, the parser attempts to
 *          continuecompilation. On a non-zero return value, the parser aborts.
 */
int pc_error(int number,const char *message,const char *filename,int firstline,int lastline,va_list argptr)
{
static const char *prefix[3]={ "error", "fatal error", "warning" };

  if (number!=0) {
    const char *pre=prefix[number/100];
    if (firstline>=0)
      fprintf(stderr,"%s(%d -- %d) : %s %03d: ",filename,firstline,lastline,pre,number);
    else
      fprintf(stderr,"%s(%d) : %s %03d: ",filename,lastline,pre,number);
  } /* if */
  vfprintf(stderr,message,argptr);
  fflush(stderr);
  return 0;
}

/** pc_opensrc()
 *  Called to open a source file (or include file) for reading.
 *
 *  \param filename     [in] The name of the "file" to read from
 *
 *  \return The function must return a pointer, which is used as a "magic
 *          cookie" to all I/O functions. When failing to open the file for
 *          reading, the function must return `NULL`.
 *
 *  \note Several "source files" may be open at the same time. For example, when
 *        the compiler plunges into an include file, the file that this was
 *        included from remain open.
 *
 *  \note This implementation reads a physical file. It opens the file in binary
 *        because functions to get/set file positions are not reliable on text
 *        files in Microsoft Windows.
 */
void *pc_opensrc(const char *filename)
{
  struct stat st;
  if (stat(filename,&st)==0 && S_ISDIR(st.st_mode))
    return NULL;
  return fopen(filename,"rt");
}

/** pc_createsrc()
 *  Called to create/overwrite a source file for writing.
 *
 *  \param filename     [in] The name of the "file" to create.
 *
 *  \return The function must return a pointer, which is used as a "magic
 *          cookie" to all I/O functions. When failing to open the file for
 *          writing, the function must return `NULL`.
 *
 *  \note Several "source files" may be open at the same time. Specifically, one
 *        file can be open for reading and another for writing.
 *
 *  \note While the function to open an existing file specifically opens it in
 *        binary mode, this function uses the default mode.
 */
void *pc_createsrc(const char *filename)
{
  return fopen(filename,"wt");
}

/** pc_closesrc()
 *  Called to close a source file (or include file).
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_opensrc()` or
 *                      `pc_createsrc()`.
 */
void pc_closesrc(void *handle)
{
  assert(handle!=NULL);
  fclose((FILE*)handle);
}

/** pc_readsrc()
 *  Called to read a single line from the source file (or up to a maximum number
 *  of characters if the line in the input file is too long).
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_opensrc()`.
 *  \param buffer       [out] Will contain the data read from the file.
 *                      This buffer is zero-terminated. If a line ends with a
 *                      newline, this '\n' character is included in the buffer.
 *  \param buffer_sz    The size of the buffer. This function will only read
 *                      'buffer_sz - 1' characters from the file, because the
 *                      zero-terminator is stored too.
 *
 *  \return `true` on success, `false` on a read error or end-of-file.
 *
 *  \note This function is very similar to `fgets()`, but it gobbles up the '\r'
 *        character on the end of the line even if the file is opened in binary
 *        mode. (This is relevant on Microsoft Window only.)
 */
bool pc_readsrc(void *handle,unsigned char *buffer,size_t buffer_sz)
{
  assert(handle!=NULL);
  assert(buffer!=NULL);
  assert(buffer_sz>0);
  size_t count=0;
  int c;
  while (count<buffer_sz-1 && (c=fgetc((FILE*)handle))>=0) {
    if (c=='\n' && count>0 && buffer[count-1]=='\r')
      count--;          /* let '\n' overwrite the '\r' */
    buffer[count++]=c;
    if (c=='\n')
      break;
  }
  assert(count<buffer_sz);
  buffer[count]='\0';
  return (count>0);
}

/** pc_writesrc()
 *  Called to write a line of text to the source file.
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_createsrc()`.
 *  \param buffer       [in] A zero-terminated text string to write to the
 *                      output. No '\n' is automatically appended.
 *
 *  \return `true` on success, `false` on failure.
 */
bool pc_writesrc(void *handle,const unsigned char *buffer)
{
  assert(handle!=NULL);
  return (fputs((char*)buffer,(FILE*)handle)>=0);
}

/** pc_getpossrc()
 *  Called to mark the current position in the source file.
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_opensrc()`.
 *
 *  \return The current position, or -1 on error.
 */
long long pc_getpossrc(void *handle)
{
  assert(handle!=NULL);
# if defined __GNUC__ || defined __clang___
    return (long long)ftello((FILE*)handle);
# elif defined _MSC_VER && defined _WIN32
    return (long long)_ftelli64((FILE*)handle);
# else
    return ftell((FILE*)handle);
# endif
}

/** pc_resetsrc()
 *  Called to reset the file 'current position' to a position marked earlier.
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_opensrc()`.
 *  \param position     The position to set. It should be a position that was
 *                      previously returned by `pc_getpossrc()`.
 *
 *  \return `true` on success, `false` on failure.
 */
bool pc_resetsrc(void *handle,long long position)
{
  assert(handle!=NULL);
  if (position<0)
    return false;
# if defined __GNUC__ || defined __clang___
    return (fseeko((FILE *)handle,position,SEEK_SET)==0);
# elif defined _MSC_VER && defined _WIN32
    return (_fseeki64((FILE *)handle,position,SEEK_SET)==0);
# else
    return (fseek((FILE *)handle,position,SEEK_SET)==0);
# endif
}

/** pc_eofsrc()
 *  Called to test whether the file 'current position' is at the end-of-file.
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_opensrc()`.
 *
 *  \return `true` on end-of-file, `false` otherwise.
 */
bool pc_eofsrc(void *handle)
{
  return feof((FILE*)handle) || ferror((FILE*)handle);
}

/** pc_openasm()
 *  Called to open/create the intermediate (pseudo-assembler) for writing and
 *  reading.
 *
 *  \param filename     [in] The name of the "file" to use.
 *
 *  \return The function must return a pointer, which is used as a "magic
 *          cookie" to all I/O functions. When failing to open the file for
 *          reading, the function must return `NULL`.
 *
 *  \note Depending on the the build, this implementation creates/opens a
 *        physical file or a pseudo-file in memory.
 */
void *pc_openasm(const char *filename)
{
  #if defined __MSDOS__ || defined PAWN_LIGHT
    return fopen(filename,"w+t");
  #else
    return mfcreate(filename);
  #endif
}

/** pc_closeasm()
 *  Called to close the intermediate (assembly) file.
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_openasm()`.
 *  \param deletefile   Whether to delete the file after closing it.
 */
void pc_closeasm(void *handle, bool deletefile)
{
  if (handle!=NULL) {
    if (!deletefile)
      mfdump((memfile_t*)handle);
    mfclose((memfile_t*)handle);
  } /* if */
}

/** pc_resetasm()
 *  Called to reset the intermediate file to the start, which is needed to
 *  switch between reading and writing.
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_openasm()`.
 */
void pc_resetasm(void *handle)
{
  assert(handle!=NULL);
  mfseek((memfile_t*)handle,0,SEEKg_SET);
}

/** pc_writeasm()
 *  Called to write a line of text to the intermediate (assembly) file.
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_openasm()`.
 *  \param buffer       [in] A zero-terminated text string to write to the
 *                      output. No '\n' is automatically appended.
 *
 *  \return The number of characters written.
 */
int pc_writeasm(void *handle,const char *buffer)
{
  assert(handle!=NULL);
  return mfputs((memfile_t*)handle,buffer);
}

/** pc_readasm()
 *  Called to read a single line from the intermediate (assembly) file (or up to
 *  a maximum number of characters if the line in the input file is too long).
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_openasm()`.
 *  \param buffer       [out] Will contain the data read from the file.
 *                      This buffer is zero-terminated. If a line ends with a
 *                      newline, this '\n' character is included in the buffer.
 *  \param buffer_sz    The size of the buffer. This function will only read
 *                      'buffer_sz - 1' characters from the file, because the
 *                      zero-terminator is stored too.
 *
 *  \return A pointer to the `buffer` parameter on success, or `NULL` on
 *          failure.
 *
 *  \note This function is very similar to `fgets()`, but it gobbles up the '\r'
 *        character on the end of the line even if the file is opened in binary
 *        mode. (This is relevant on Microsoft Window only.)
 */
char *pc_readasm(void *handle, char *buffer, int buffer_sz)
{
  assert(handle!=NULL);
  return mfgets((memfile_t*)handle,buffer,buffer_sz);
}

/** pc_openbin()
 *  Called to create the P-code for writing and reading.
 *
 *  \param filename     [in] The name of the "file" to create.
 *
 *  \return The function must return a pointer, which is used as a "magic
 *          cookie" to all I/O functions. When failing to open the file for
 *          reading, the function must return `NULL`.
 */
void *pc_openbin(const char *filename)
{
  return fopen(filename,"wb");
}

/** pc_closebin()
 *  Called to close the P-code file.
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_openbin()`.
 *  \param deletefile   Whether to delete the file after closing it, or whether
 *                      to keep it.
 */
void pc_closebin(void *handle,int deletefile)
{
  fclose((FILE*)handle);
  if (deletefile)
    remove(binfname);
}

/** pc_resetbin() is an interface function to set the position in the P-code
 *  file.
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_openbin()`.
 *  \param position     The offset from the start of the file.
 */
bool pc_resetbin(void *handle,long long position)
{
  fflush((FILE*)handle);
  if (position<0)
    return false;
# if defined __GNUC__ || defined __clang___
    return (fseeko((FILE *)handle,position,SEEK_SET)==0);
# elif defined _MSC_VER && defined _WIN32
    return (_fseeki64((FILE *)handle,position,SEEK_SET)==0);
# else
    return (fseek((FILE *)handle,position,SEEK_SET)==0);
# endif
}

bool pc_writebin(void *handle,const void *buffer,int size)
{
  return (int)fwrite(buffer,1,size,(FILE*)handle) == size;
}

long long pc_lengthbin(void *handle)
{
  assert(handle!=NULL);
# if defined __GNUC__ || defined __clang___
    _off_t curpos=ftello((FILE*)handle);
    fseeko((FILE*)handle,0,SEEK_END);
    _off_t tail=ftello((FILE*)handle);
    fseeko((FILE*)handle,curpos,SEEK_SET);
    return (long long)tail;
# elif defined _MSC_VER && defined _WIN32
    __int64 curpos=_ftelli64((FILE*)handle);
    _fseeki64((FILE*)handle,0,SEEK_END);
    __int64 tail=_ftelli64((FILE*)handle);
    _fseeki64((FILE*)handle,curpos,SEEK_SET);
    return (long long)tail;
# else
    long  curpos=ftell((FILE*)handle);
    fseek((FILE*)handle,0,SEEK_END);
    long tail=ftell((FILE*)handle);
    fseek((FILE*)handle,curpos,SEEK_SET);
    return (long long)tail;
# endif

