/*  Pawn compiler
 *
 *  Pawn is a scripting language system consisting of a compiler and an
 *  abstract machine, for building and running programs in the Pawn language.
 *
 *  The Pawn compiler has its origins in the Small-C compiler Version 2.01,
 *  originally created by Ron Cain, july 1980, and enhanced by James E. Hendrix.
 *  Although the Pawn compiler comes close to a complete rewrite of Small-C,
 *  its origins are still clearly visible.
 *
 *  Copyright (c) 1997-2026, CompuPhase and contributors
 *  Copyright (c) 1982, 1983, J.E. Hendrix
 *  Copyright (c) 1980, R. Cain
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
 *  Version: $Id: sc1.c 7717 2026-10-17 16:25:40Z thiadmer $
 */
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if !defined S_ISDIR
# define S_ISDIR(mode) (((mode) & _S_IFDIR) != 0)
#endif

#if defined __WIN32__ || defined _WIN32 || defined __MSDOS__
# include <conio.h>
# include <io.h>
# include <process.h>
#else
# include <spawn.h>
#endif
#if defined __GNUC__ || defined __clang__
# include <unistd.h>
#endif

#if defined FORTIFY
# include <alloc/Fortify.h>
#endif

#if defined __BORLANDC__ || defined __WATCOMC__
# include <dos.h>
  static unsigned total_drives; /* dummy variable */
# define dos_setdrive(i)       _dos_setdrive(i,&total_drives)
#elif defined _MSC_VER && defined _WIN32
# include <direct.h>           /* for _chdrive() */
# define dos_setdrive(i)       _chdrive(i)
#endif
#if defined __BORLANDC__
# include <dir.h>              /* for chdir() */
#elif defined __WATCOMC__
# include <direct.h>           /* for chdir() */
#endif
#if defined __WIN32__ || defined _WIN32 || defined _Windows
# include <windows.h>
#endif

#if defined __WIN32__ || defined _WIN32 || defined WIN32 || defined __NT__
# define EXPORT __declspec (dllexport)
#elif defined HAVE_VISIBILITY
# define EXPORT __attribute__ ((visibility("default")))
#endif

#include "lstring.h"
#include "sc.h"
#if defined __LINUX__ || defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__ || defined __APPLE__
# include <sys/wait.h>
# include <sclinux.h>
# include <sys/wait.h>
#endif
#if defined __LINUX__
# include <binreloc.h> /* from BinReloc, see www.autopackage.org */
#elif defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__
# include <installpath.h>
#endif

#include "svnrev.h"
#define VERSION_STR "4.2." SVNREV_STR
#define VERSION_INT 0x0402


static void resetglobals(void);
static void initglobals(void);
static char *get_extension(const char *filename);
static char *add_path_prefix(char *fullpath,size_t maxsize,const char *path,const char *filename);
static void setopt(int argc,char **argv,char *oname,char *ename,char *pname,
                   char *rname,char *codepage);
static void setconfig(const char *root);
static void setcaption(int linespacing);
static void about(void);
static void setconstants(void);
static void plungeprefix(char *prefixname);
static void parse(void);
static void dumplits(void);
static void dumpzero(int count,const char *label);
static void declfuncvar(bool fpublic,bool fstatic,bool fstock,bool fconst);
static bool declglb(const char *firstname,int firsttag,bool fpublic,bool fstatic,
                    bool fstock,bool fconst);
static bool declloc(bool fstatic);
static void decl_const(int scope);
static cell needsub(char match,constvalue **namelist);
static void verify_array_namelist(constvalue *namelist[], int numdim);
static void initials(int ident,int usage,int tag,cell *size,int dim[],int numdim,
                     constvalue *dimnames[]);
static cell initarray(int ident,int usage,int tag,int dim[],int numdim,int cur,
                      int startlit,int counteddim[],constvalue *lastdim,
                      constvalue *dimnames[],bool *errorfound);
static cell initvector(int ident,short usage,int tag,cell size,int fillzero,
                       constvalue *dimnames,bool *errorfound);
static cell init(int ident,int usage,int *tag,bool *errorfound,int *packcount,cell *packitem);
static int getstates(const char *symname);
static statelist *attachstatelist(symbol *sym,int state_id);
static void funcstub(int fnative);
static int newfunc(const char *firstname,int firsttag,int fpublic,int fstatic,int stock);
static int declparams(symbol *sym,int chkshadow);
static void param2symbol(const char *name,int ident,int offset,const int tags[],int numtags,
                         bool fpublic,bool fconst,bool chkshadow,arginfo *arg);
static bool vararg_locals(symbol *sym);
static void make_report(symbol *root,FILE *log,const char *sourcefile,int *makestategraph);
static void reduce_referrers(symbol *root);
static void gen_ovlinfo(symbol *root);
static long max_stacksize(symbol *root,bool *recursion);
static long max_overlaysize(symbol *root,char **funcname);
static bool checkundefined(symbol *root);
static bool testsymbols(const symbol *root,short level,bool testfunclvl,bool testconst);
static int test_skippedundef(void);
static void destructsymbols(symbol *root,int level);
static constvalue *find_constval_byval(constvalue *table,cell val);
static constvalue *clone_consttable(const constvalue *table);
static symbol *fetchlab(const char *name);
static void statement(int *lastindent,bool allow_decl);
static void statement_nested(int *lastindent,bool allow_decl);
static void compound(const unsigned char *stmt_sameline);
static int test(int label,bool invert,bool test_true,bool *parenthesized);
static int doexpr(int comma,int chkeffect,int allowarray,int mark_endexpr,
                  int *tag,symbol **symptr,bool chkfuncresult,int in_test);
static void doassert(void);
static void doexit(void);
static int doif(void);
static int dowhile(void);
static int dodo(void);
static int dofor(void);
static void doswitch(void);
static void dogoto(void);
static void dolabel(void);
static void doreturn(void);
static void dobreak(void);
static void docont(void);
static void dosleep(void);
static void dostate(void);
static void addwhile(int *ptr);
static void delwhile(void);
static int *readwhile(void);

static int lastst      =0;        /**< last executed statement type */
static int endlessloop =0;        /**< nesting level of endless loop */
static int rettype     =0;        /**< the type that a "return" expression should have */
static int skipinput   =0;        /**< number of lines to skip from the first input file */
static int verbosity   =1;        /**< verbosity level, 0=quiet, 1=normal, 2=verbose */
static bool sc_reparse =false;    /**< needs 3th parse because of changed prototypes? */
static int sc_parsenum =0;        /**< number of the extra parses */
static bool undefined_vars=false; /**< if true, undefined symbols were found */
static int pc_enumsequence=0;     /**< sequence number of enumerated constant lists, for reporting these lists */
static int wq_glb[wqTABSZ];       /**< "while queue", internal stack for nested loops */
static int *wq_ptr;               /**< pointer to next entry */
#if !defined PAWN_LIGHT
  static char sc_rootpath[_MAX_PATH]; /**< base path of the installation */
  static char sc_binpath[_MAX_PATH];  /**< path for the binaries, often sc_rootpath + /bin */
  static char *pc_globaldoc=NULL;     /**< main documentation */
  static char *pc_recentdoc=NULL;     /**< documentation from the most recent comment block */
  int pc_docstring_suspended=false;
#endif


/* If you want to embed the compiler into your aplication, there are two steps:
 *
 *  1. You build the sources with the macro 'PAWN_NO_MAIN'.
 *  2. You implement a series of interface functions for reading and writing
 *     files, printing error messages, and similar. These are documented (in
 *     comments) in the block below.
 */
#if !defined PAWN_NO_MAIN

#if defined __TURBOC__ && !defined __32BIT__
  extern unsigned int _stklen = 0x2000;
#endif

int main(int argc, char *argv[])
{
  return pc_compile(argc,argv);
}

/** pc_printf()
 *  Called for general purpose "console" output. This function prints general
 *  purpose messages; errors go through pc_error(). The function is modelled
 *  after printf().
 */
int pc_printf(const char *message,...)
{
  va_list argptr;
  va_start(argptr,message);
  int ret=vprintf(message,argptr);
  va_end(argptr);
  fflush(stdout);

  return ret;
}

/** pc_error()
 *  Called for producing error output.
 *
 *  \param number       The error number (as documented in the manual). If set
 *                      to zero, the parameters for the error location are not
 *                      relevant.
 *  \param prefix       [in] The message prefix, one of "error", "fatal error"
 *                      or "warning". Ignored if `number` is zero.
 *  \param message      [in] A string describing the error, with embedded '%d'
 *                      and '%s' tokens if applicable.
 *  \param filename     [in] The name of the file currently being parsed.
 *                      Ignored if `number` is zero.
 *  \param firstline    The line number at which the expression started on which
 *                      the error was found, or -1 if there is no "starting
 *                      line". Ignored if `number` is zero.
 *  \param lastline     The line number at which the error was detected.
 *                      Ignored if `number` is zero.
 *  \param argptr       a pointer to the first of a series of arguments (for
 *                      macro "va_arg").
 *
 *  \return If the function returns 0, the parser attempts to continue
 *          compilation. On a non-zero return value, the parser aborts.
 */
int pc_error(int number,const char *prefix,const char *message,
             const char *filename,int firstline,int lastline,va_list argptr)
{
  if (number!=0) {
    if (firstline>=0)
      fprintf(stderr,"%s(%d -- %d) : %s %03d: ",filename,firstline,lastline,prefix,number);
    else
      fprintf(stderr,"%s(%d) : %s %03d: ",filename,lastline,prefix,number);
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
  return fopen(filename,"rb");
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
  assert(filename!=NULL);
  return fopen(filename,"w");
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
 *  Called to open/create the intermediate (pseudo-assembler) file for both
 *  writing and reading.
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
# if defined __MSDOS__ || defined PAWN_LIGHT
    return fopen(filename,"w+");
# else
    return mfcreate(filename);
# endif
}

/** pc_closeasm()
 *  Called to close the intermediate (assembly) file.
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_openasm()`.
 *  \param deletefile   Whether to delete the file after closing it, or whether
 *                      to keep it.
 */
void pc_closeasm(void *handle, bool deletefile)
{
# if defined __MSDOS__ || defined PAWN_LIGHT
    if (handle!=NULL)
      fclose((FILE*)handle);
    if (deletefile)
      remove(outfname);
# else
    if (handle!=NULL) {
      if (!deletefile)
        mfdump((memfile_t*)handle);
      mfclose((memfile_t*)handle);
    } /* if */
# endif
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
# if defined __MSDOS__ || defined PAWN_LIGHT
    fflush((FILE*)handle);
    fseek((FILE*)handle,0,SEEK_SET);
# else
    mfseek((memfile_t*)handle,0,SEEK_SET);
# endif
}

/** pc_writeasm()
 *  Called to write a line of text to the intermediate (assembly) file.
 *
 *  \param handle       The "handle" for the file, a value that must have been
 *                      returned by an earlier call to `pc_openasm()`.
 *  \param buffer       [in] A zero-terminated text string to write to the
 *                      output. No '\n' is automatically appended.
 *
 *  \return `true` on success, `false` on failure.
 */
bool pc_writeasm(void *handle,const char *buffer)
{
  #if defined __MSDOS__ || defined PAWN_LIGHT
    return fputs(buffer,(FILE*)handle) >= 0;
  #else
    return mfputs((memfile_t*)handle,buffer);
  #endif
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
  #if defined __MSDOS__ || defined PAWN_LIGHT
    return fgets(buffer,buffer_sz,(FILE*)handle);
  #else
    return mfgets((memfile_t*)handle,buffer,buffer_sz);
  #endif
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
void pc_closebin(void *handle,bool deletefile)
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
    off_t curpos=ftello((FILE*)handle);
    fseeko((FILE*)handle,0,SEEK_END);
    off_t tail=ftello((FILE*)handle);
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
}

#endif  /* !defined PAWN_NO_MAIN */


#if !(defined __MSDOS__ || defined __WIN32__ || defined _Windows)
int posix_spawnl(char *pgm,...)
{
# define SPAWN_MAX_ARGS  10
  pid_t pid;
  va_list ap;
  char *ptr;
  char *argv[SPAWN_MAX_ARGS];
  char *envp[] = { NULL };
  int stat,i;

  argv[0]=pgm;
  for (i=1, va_start(ap,pgm); (ptr=va_arg(ap,char*))!=NULL && i<SPAWN_MAX_ARGS; i++)
    argv[i]=ptr;
  argv[i]=NULL;
  va_end(ap);

  stat=posix_spawn(&pid,pgm,NULL,NULL,argv,envp);
  if (stat==0)
    waitpid(pid,&stat,0);
  return stat;
}
#endif

/*  "main" of the compiler
 */
#if defined __cplusplus
  extern "C"
#endif
EXPORT
int pc_compile(int argc, char *argv[])
{
  int entry,i,jmpcode;
  int retcode,lbl_nostate,lbl_exitstate;
  char incfname[_MAX_PATH];
  char reportname[_MAX_PATH];
  char codepage[MAXCODEPAGE+1];
  /* assigned after setjmp() and read again after the longjmp() to "cleanup",
   * so they must be volatile (C99 7.13.2.1p3)
   */
  char * volatile tmpname;  /* temporary input file name */
  FILE * volatile binf;
  int lcl_tabsize;
  char *ptr;

  /* set global variables to their initial value */
  binf=NULL;
  tmpname=NULL;
  initglobals();
  errorset(sRESET);
  errorset(sEXPRRELEASE);
  if (!lexinit(false))
    error(303);         /* insufficient memory */

  /* make sure that we clean up on a fatal error; do this before the first
   * call to error(). */
  if ((jmpcode=setjmp(errbuf))!=0)
    goto cleanup;

  inpfname=(char*)malloc(_MAX_PATH*sizeof(char));
  if (inpfname==NULL)
    error(303);         /* insufficient memory */
  *inpfname='\0';
  if (!phopt_init())
    error(303);         /* insufficient memory */

  setconfig(argv[0]);   /* the path to the include and codepage files, plus the root path */
  setopt(argc,argv,outfname,errfname,incfname,reportname,codepage);
  strcpy(binfname,outfname);
  ptr=get_extension(binfname);
  if (ptr!=NULL && stricmp(ptr,".asm")==0)
    set_extension(binfname,".amx",true);
  else
    set_extension(binfname,".amx",false);
  /* set output names that depend on the input name */
  if (pc_listing)
    set_extension(outfname,".lst",true);
  else
    set_extension(outfname,".asm",true);
  if (strlen(errfname)!=0)
    remove(errfname);   /* delete file on startup */
  else if (verbosity>0)
    setcaption(true);
  lcl_tabsize=pc_tabsize;
# if !defined PAWN_NO_CODEPAGE
    if (!cp_set(codepage))      /* set codepage */
      error(308);               /* codepage mapping file not found */
# endif
  /* optionally create a temporary input file that is a collection of all
   * input files
   */
  assert(get_sourcefile(0)!=NULL);  /* there must be at least one source file */
  if (get_sourcefile(1)!=NULL) {
    /* there are at least two or more source files */
    /* ??? it would be better to create the temporay file in memory */
    const char *sname;
    FILE *ftmp;
    int fidx;
#   if defined __WIN32__ || defined _WIN32
      tmpname=strdup(_tempnam(NULL,"pawn"));
#   elif defined __MSDOS__ || defined _Windows
      tmpname=strdup(tempnam(NULL,"pawn"));
#   elif defined __LINUX__ || defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__ || defined __APPLE__
      char mask[]="pawn.XXXXXX";
      int tmpfd=mkstemp(mask);
      char procname[64];
      snprintf(procname,countof(procname),"/proc/self/fd/%d",tmpfd);
      char *linkbuf=alloca(PATH_MAX);  /* allocate maximum size buffer, it is temporary */
      int linklen=readlink(procname,linkbuf,PATH_MAX);
      close(tmpfd);
      if (linklen>0 && linklen<PATH_MAX) {
        linkbuf[linklen]='\0';  /* readlink() does not NUL-terminate its output, and may return -1 */
        tmpname=strdup(linkbuf);
      } else {
        error(313,"multiple sources on the command line");  /* unsupported feature */
      }
#   else
      error(313,"multiple sources on the command line");  /* unsupported feature */
#   endif
    if (!tmpname)
      error(303);       /* insufficient memory */
    ftmp=(FILE*)pc_createsrc(tmpname);  /* error 303 is a fatal error, so tmpname is non-NULL when arriving here */
    for (fidx=0; (sname=get_sourcefile(fidx))!=NULL; fidx++) {
      unsigned char tstring[128];
      FILE *fsrc=(FILE*)pc_opensrc(sname);
      if (fsrc==NULL) {
        pc_closesrc(ftmp);
        remove(tmpname);
        strcpy(inpfname,sname); /* avoid invalid filename */
        error(300,sname);       /* fatal error, cannot read from file */
      } /* if */
      pc_writesrc(ftmp,(unsigned char*)"#file \"");
      pc_writesrc(ftmp,(unsigned char*)sname);
      pc_writesrc(ftmp,(unsigned char*)"\"\n#line 0\n");
      while (pc_readsrc(fsrc,tstring,sizeof tstring))
        pc_writesrc(ftmp,tstring);
      if (strchr((char*)tstring,'\n')==NULL) /* force newline on last line of each input file */
        pc_writesrc(ftmp,(const unsigned char*)"\n");
      pc_closesrc(fsrc);
    } /* for */
    pc_closesrc(ftmp);
    strcpy(inpfname,tmpname);
  } else {
    assert(tmpname==NULL);
    strcpy(inpfname,get_sourcefile(0));
  } /* if */
  inpf_org=(FILE*)pc_opensrc(inpfname);
  if (inpf_org==NULL)
    error(300,inpfname);        /* cannot read from file */
  freading=true;
  sc_is_utf8=(short)scan_utf8(inpf_org,inpfname);
  outf=(FILE*)pc_openasm(outfname); /* first write to assembler file (may be temporary) */
  if (outf==NULL)
    error(301,outfname);        /* cannot write to file */
  /* immediately open the binary file, for other programs to check */
  if (pc_asmfile || pc_listing) {
    binf=NULL;
  } else {
    binf=(FILE*)pc_openbin(binfname);
    if (binf==NULL)
      error(301,binfname);      /* cannot write to file */
  } /* if */
  setconstants();               /* set predefined constants and tagnames */
  for (i=0; i<skipinput; i++)   /* skip lines in the input file */
    if (pc_readsrc(inpf_org,srcline,sLINEMAX))
      pc_curline++;             /* keep line number up to date */
  skipinput=pc_curline;
  sc_status=statBROWSE;
  /* write starting options (from the command line or the configuration file) */
  if (pc_listing) {
    char string[150];
    if (pc_matchedtabsize>1 && pc_tabsize>1)
      snprintf(string,countof(string),"#pragma tabsize %d\n",pc_tabsize);
    pc_writeasm(outf,string);
    setfiledirect(inpfname);
  } /* if */
  /* do the first pass through the file (or possibly two or more "first passes") */
  sc_parsenum=0;
  long long inpfmark=pc_getpossrc(inpf_org);
  char inpfname_org[_MAX_PATH]; /* save name of the main input file so it can be restored after each pass */
  strlcpy(inpfname_org,inpfname,sizeof inpfname_org);
  do {
    /* reset "defined" flag of all functions and global variables */
    reduce_referrers(&glbtab);
    delete_symbols(&glbtab,0,true,false);
    delete_heaplisttable();
#   if !defined NO_DEFINE
      delete_substtable();
#   endif
    delete_inputfiletable();
    delete_undefsymboltable();
    litarray_deleteall();
    resetglobals();
    pc_tabsize=(pc_matchedtabsize<=1) ? lcl_tabsize : pc_matchedtabsize;
    errorset(sRESET);
    /* reset the source file */
    inpf=inpf_org;
    freading=true;
    pc_resetsrc(inpf,inpfmark); /* reset file position */
    pc_curline=skipinput;       /* reset line number */
    sc_status=statBROWSE;       /* resetglobals() resets it to IDLE */

    insert_inputfile(inpfname); /* save for the error system and the report mechanism */
    plungeprefix(incfname);     /* jump into "default.inc" or alternative prefix file */
    preprocess();               /* fetch first line */
    parse();                    /* process all input */
    sc_parsenum++;
    /* if the name of the main source file was renamed with a #file directive,
     * the filename must be restored
     */
    if (strcmp(inpfname,inpfname_org)!=0) {
      free(inpfname);
      inpfname=duplicatestring(inpfname_org);
      if (inpfname==NULL)
        error(303);             /* insufficient memory */
    } /* if */
    if (test_skippedundef()>0)
      sc_reparse=true;          /* conditional compiled code sections may have changed */
  } while (sc_reparse);

  /* after the "discovery" passes, test the global symbol table for undefined
   * symbols
   */
  undefined_vars=checkundefined(&glbtab);

  /* second pass (or third if there were multiple discovery passes) */
  sc_status=statWRITE;          /* set, to enable warnings */
  state_conflict(&glbtab);

  /* write a report, if requested */
# if !defined PAWN_LIGHT
    if (sc_makereport) {
      if (strlen(reportname)>0) {
        int makestategraph=0;
        FILE *frep=fopen(reportname,"wb");  /* avoid translation of \n to \r\n in DOS/Windows */
        if (frep!=NULL) {
          make_report(&glbtab,frep,get_sourcefile(0),&makestategraph);
          fclose(frep);
        } /* if */
        if (makestategraph) {
          /* run stategraph to create a dot file */
          char dotname[_MAX_PATH];
          char pgmname[_MAX_PATH];
          strcpy(dotname,reportname);
          set_extension(dotname,".dot",true);
          add_path_prefix(pgmname,countof(pgmname),sc_binpath,"stategraph");
#         if defined __MSDOS__ || defined __WIN32__ || defined _Windows
            set_extension(pgmname,".exe",true);
            spawnl(P_WAIT,pgmname,pgmname,reportname,dotname,NULL);
#         else
            posix_spawnl(pgmname,reportname,dotname,(void*)0);
#         endif
        }
      } /* if */
      if (pc_globaldoc!=NULL) {
        free(pc_globaldoc);
        pc_globaldoc=NULL;
      } /* if */
      /* a comment block that no declaration ever claimed (the declaration that
       * would have taken it failed to parse) is still ours to release
       */
      if (pc_recentdoc!=NULL) {
        free(pc_recentdoc);
        pc_recentdoc=NULL;
      } /* if */
    } /* if */
# endif

  if (!pc_listing) {
#   if !defined NO_DEFINE
      delete_substtable();
#   endif
    delete_inputfiletable();
    delete_undefsymboltable();
    litarray_deleteall();
    resetglobals();
    pc_tabsize=(pc_matchedtabsize<=1) ? lcl_tabsize : pc_matchedtabsize;
    errorset(sRESET);
    /* reset the source file */
    inpf=inpf_org;
    freading=true;
    pc_resetsrc(inpf,inpfmark); /* reset file position */
    pc_curline=skipinput;       /* reset line number */
    if (!lexinit(false))        /* clear internal flags of lex() */
      error(303);               /* insufficient memory */
    sc_status=statWRITE;        /* allow to write --this variable was reset by resetglobals() */
    writeleader(&glbtab,&lbl_nostate,&lbl_exitstate);
    reduce_referrers(&glbtab);  /* test for unused functions */
    gen_ovlinfo(&glbtab);       /* generate overlay information */
    writestatetables(&glbtab,lbl_nostate,lbl_exitstate);  /* create state tables and additional overlay information */
    /* reset "defined" flag of all functions and global variables */
    delete_symbols(&glbtab,0,true,false);
    insert_dbgfile(inpfname);   /* attach to debug information */
    insert_inputfile(inpfname); /* save for the error system */
    plungeprefix(incfname);     /* jump into "default.inc" or alternative prefix file */
    preprocess();               /* fetch first line */
    parse();                    /* process all input */
    /* inpf is already closed when readline() attempts to pop of a file */
    writetrailer();             /* write remaining stuff */

    entry=testsymbols(&glbtab,0,true,false);  /* test for unused or undefined
                                               * functions and variables */
    if (!entry)
      error(13);                  /* no entry point (no public functions) */
  }

cleanup:
  /* also the target of the longjmp() on a fatal error, meaning that this code
   * may be re-entered if a fatal error occurs during assembly or report
   * generation
   */
  if (inpf!=NULL) {             /* main source file is not closed, do it now */
    pc_closesrc(inpf);
    inpf=NULL;
  } /* if */
  /* write the binary file (the file is already open) */
# if !defined PAWN_LIGHT
    int hdrsize=0;
# endif
  if (!(pc_asmfile || pc_listing) && errorcount(NULL,NULL)<2 && jmpcode==0) {
    assert(binf!=NULL);
    pc_resetasm(outf);          /* flush and loop back, for reading */
#   if !defined PAWN_LIGHT
      hdrsize=
#   endif
    assemble(binf,outf);        /* assembler file is now input */
  } /* if */
  if (outf!=NULL) {
    pc_closeasm(outf,!(pc_asmfile || pc_listing));
    outf=NULL;
  } /* if */
  if (binf!=NULL) {
    pc_closebin(binf,(errorcount(NULL,NULL)>=2));
    binf=NULL;
  } /* if */

# if !defined PAWN_LIGHT
    if (errorcount(NULL,NULL)<2 && strlen(errfname)==0) {
      bool recursion;
      int flag_exceed=0;
      long stacksize=max_stacksize(&glbtab,&recursion);
      char *max_ovlname=NULL;
      long max_ovlsize=(pc_overlays>0) ? max_overlaysize(&glbtab,&max_ovlname) : 0;
      long totalsize=hdrsize;
      if (pc_overlays==0)
        totalsize+=(long)code_idx;
      else if (pc_overlays==1)
        totalsize+=max_ovlsize;
      else
        totalsize+=pc_overlays;
      if (pc_amxram==0)
        totalsize+=(long)(glb_declared+pc_stksize)*pc_cellsize;
      if (pc_amxlimit>0 && totalsize>=pc_amxlimit)
        flag_exceed=1;
      if (pc_amxram>0 && (glb_declared+pc_stksize)*pc_cellsize>=pc_amxram)
        flag_exceed=1;
      if (((sc_debug & sSYMBOLIC)!=0 && verbosity!=0) || verbosity>=2 || stacksize+32>=(long)pc_stksize || flag_exceed) {
        if (errorcount(NULL,NULL)==1)
          pc_printf("\n");
        pc_printf("Header size:       %8lu bytes\n",(long)hdrsize);
        pc_printf("Code size:         %8lu bytes\n",(long)code_idx);
        if (pc_overlays>0) {
          if (pc_overlays>1)
            pc_printf("Max. overlay size: %8lu bytes; largest overlay=%ld bytes\n",(long)pc_overlays,(long)max_ovlsize);
          else
            pc_printf("Largest overlay:   %8lu bytes\n",(long)max_ovlsize);
        } /* if */
        pc_printf("Data size:         %8lu bytes\n",(long)glb_declared*pc_cellsize);
        pc_printf("Stack/heap size:   %8lu bytes; ",(long)pc_stksize*pc_cellsize);
        pc_printf("estimated max. use");
        if (recursion)
          pc_printf(": unknown, due to recursion\n");
        else if ((pc_memflags & suSLEEP_INSTR)!=0)
          pc_printf(": unknown, due to \"sleep\" instruction\n");
        else
          pc_printf("=%lu cells (%lu bytes)\n",stacksize,stacksize*pc_cellsize);
        pc_printf("Total requirements:%8lu bytes",(long)totalsize);
        if (pc_amxram>0)
          pc_printf(" plus %lu bytes for data/stack",(long)(glb_declared+pc_stksize)*pc_cellsize);
        pc_printf("\n");
      } /* if */
      if (pc_overlays>1 && max_ovlsize>pc_overlays) {
        char symname[2*sNAMEMAX+16];  /* allow space for user defined operators */
        assert(max_ovlname!=NULL);
        funcdisplayname(symname,countof(symname),max_ovlname);
        error(312,symname,(max_ovlsize-pc_overlays)*pc_cellsize); /* overlay exceeds limit */
      } /* if */
      if (flag_exceed)
        error(306,pc_amxlimit+pc_amxram); /* this causes a jump back to label "cleanup" */
    } /* if */
# endif

  litq_free();
  /* when aborting inside an include file, pop off and erase all names on the stack */
  while ((i=POPSTK_I())!=-1) {
    (void)POPSTK_I();   /* fcurrent */
    (void)POPSTK_I();   /* icomment */
    (void)POPSTK_I();   /* sc_is_utf8 */
    (void)POPSTK_I();   /* iflevel */
    (void)POPSTK_P();   /* curlibrary */
    inpfname=(char *)POPSTK_P();
    inpf=(FILE *)POPSTK_P();
    assert(inpfname!=NULL && inpfname!=(char*)-1);
    free(inpfname);
    assert(inpf!=NULL && inpf!=(FILE*)-1);
    pc_closesrc(inpf);
  } /* while */
  lexinit(true);                          /* reset and release buffers */
  phopt_cleanup();
  stgbuffer_cleanup();
  clearstk();
  clear_warningstack();
  if (tmpname!=NULL) {
    remove(tmpname);
    free(tmpname);
    tmpname=NULL;
  } /* if */
  if (inpfname!=NULL) {
    free(inpfname);
    inpfname=NULL;
  }
  assert(jmpcode!=0 || loctab.next==NULL);/* on normal flow, local symbols
                                           * should already have been deleted */
  delete_symbols(&loctab,0,true,true);    /* delete local variables if not yet
                                           * done (i.e. on a fatal error) */
  delete_symbols(&glbtab,0,true,true);
  delete_consttable(&tagname_tab);
  delete_consttable(&libname_tab);
  delete_consttable(&ntvindex_tab);
  delete_consttable(&sc_automaton_tab);
  delete_consttable(&sc_state_tab);
  state_deletetable();
  delete_aliastable();
  delete_pathtable();
  delete_sourcefiletable();
  delete_inputfiletable();
  delete_undefsymboltable();
  delete_dbgstringtable();
  litarray_deleteall();
# if !defined NO_DEFINE
    delete_substtable();
# endif
# if !defined PAWN_LIGHT
    delete_docstringtable();
    if (pc_globaldoc!=NULL) {
      free(pc_globaldoc);
      pc_globaldoc=NULL;
    }
    if (pc_recentdoc!=NULL) {
      free(pc_recentdoc);
      pc_recentdoc=NULL;
    }
# endif
  delete_autolisttable();
  delete_heaplisttable();
  int errnum,warnnum;
  int errlevel=errorcount(&errnum,&warnnum);
  if (errnum!=0) {
    assert(errlevel==2 || errnum>1);  /* when errors are hidden, there must be more than 1 */
    if (strlen(errfname)==0) {
      if (errlevel==3)
        pc_printf("\n%d Errors (not all errors are shown).\n",errnum);
      else
        pc_printf("\n%d Error%s.\n",errnum,(errnum>1) ? "s" : "");
    } /* if */
    retcode=1;
  } else if (warnnum!=0){
    if (strlen(errfname)==0)
      pc_printf("\n%d Warning%s.\n",warnnum,(warnnum>1) ? "s" : "");
    retcode=0;          /* use "0", so that MAKE and similar tools continue */
  } else {
    if (verbosity>0)
      pc_printf("Completed successfully\n");
    retcode = jmpcode;
  } /* if */
# if defined __LINUX__
    br_deinit();
# endif
# if defined FORTIFY
    Fortify_ListAllMemory();
# endif
  return retcode;
}

#if defined __cplusplus
  extern "C"
#endif
EXPORT
int pc_addconstant(const char *name,cell value,int tag)
{
  errorset(sFORCESET);  /* make sure error engine is silenced */
  sc_status=statIDLE;
  add_constant(name,value,sGLOBAL,tag);
  return 1;
}

#if defined __cplusplus
  extern "C"
#endif
EXPORT
int pc_addtag(const char *name)
{
  cell val;
  constvalue *ptr;
  int last,tag;

  if (name==NULL) {
    /* no tagname was given, check for one */
    if (lex(&val,(char**)&name)!=tLABEL) {
      lexpush();
      return 0;         /* untagged */
    } /* if */
  } /* if */

  assert(strchr(name,':')==NULL); /* colon should already have been stripped */
  last=0;
  ptr=tagname_tab.next;
  while (ptr!=NULL) {
    tag=(int)(ptr->value & TAGMASK);
    if (strcmp(name,ptr->name)==0)
      return tag;       /* tagname is known, return its sequence number */
    tag &= (int)~FIXEDTAG;
    if (tag>last)
      last=tag;
    ptr=ptr->next;
  } /* while */

  /* tagname currently unknown, add it */
  tag=last+1;           /* guaranteed not to exist already */
  if (isupper(*name))
    tag |= (int)FIXEDTAG;
  append_constval(&tagname_tab,name,(cell)tag,0);
  return tag;
}

static void resetglobals(void)
{
  /* reset the subset of global variables that is modified by the first pass */
  errorset(sEXPRRELEASE);
  set_deprecated(NULL,false);
  curfunc=NULL;         /* pointer to current function */
  lastst=0;             /* last executed statement type */
  rettype=0;            /* the type that a "return" expression should have */
  litidx=0;             /* index to literal table */
  stgidx=0;             /* index to the staging buffer */
  sc_labnum=0;          /* top value of (internal) labels */
  staging=false;        /* true if staging output */
  lcl_declared=0;       /* number of local cells declared */
  glb_declared=0;       /* number of global cells declared */
  code_idx=0;           /* number of bytes with generated code */
  ntv_funcid=0;         /* incremental number of native function */
  curseg=0;             /* 1 if currently parsing CODE, 2 if parsing DATA */
  freading=false;       /* no input file ready yet */
  pc_curline=0;         /* the line number in the current file */
  fnumber=0;            /* the file number in the file table (debugging) */
  fcurrent=0;           /* current file being processed (debugging) */
  pc_blocklevel=0;      /* level of active (nested) compound statements */
  pc_sideeffect=false;  /* true if an expression causes a side-effect */
  pc_stmtindent=0;      /* current indent of the statement */
  pc_exprnesting=0;     /* nesting of the parser, also after a fatal error */
  pc_stmtnesting=0;
  indent_nowarn=false;  /* do not skip warning "217 loose indentation" */
  sc_allowtags=true;    /* allow/detect tagnames */
  sc_status=statIDLE;   /* browse/write/skip status */
  sc_allowproccall=false; /* allow/detect tagnames in lex() */
  pc_pendingflags=0;    /* operations to perform on next function/data declaration (bit set) */
  sc_reparse=false;     /* assume no extra pass needed */
  pc_docexpr=false;
  sc_curstates=0;
  pc_memflags=0;
  pc_enumsequence=0;
  curlibrary=NULL;      /* current library name, selected by '#pragma library' */
}

static void initglobals(void)
{
  resetglobals();
  errorset(sRESETERRCOUNT);

  pc_asmfile=false;     /* do not create .ASM file */
  pc_listing=false;     /* do not create .LST file */
  skipinput=0;          /* number of lines to skip from the first input file */
  verbosity=1;          /* verbosity level, 0=no copyright banner */
  sc_debug=sCHKBOUNDS;  /* by default: bounds checking+assertions */
  pc_optimize=sOPTIMIZE_CORE;
  pc_cellsize=4;        /* default cell size = 4 bytes, 32-bits */
  pc_dataalign=pc_cellsize;
  pc_stksize=sDEF_AMXSTACK;/* default stack size */
  pc_addlibtable=true;  /* by default, add a "library table" to the output file */
  pc_amxlimit=0;        /* no limit on size of the abstract machine */
  pc_amxram=0;          /* no limit on data size of the abstract machine */
  pc_tabsize=8;         /* assume a TAB is 8 spaces */
  pc_matchedtabsize=0;  /* allow auto-adjust of TAB size (no space indents detected yet) */
  undefined_vars=false; /* if true, undefined symbols were found */
  pc_overlays=0;        /* do not generate for overlays */
  for (int i=0; i<ovlFIRST; i++)
    pc_ovl0size[i][0]=pc_ovl0size[i][1]=0;
  pc_cryptkey=0;

  outfname[0]='\0';     /* output file name */
  errfname[0]='\0';     /* error file name */
  inpf=NULL;            /* file read from */
  inpfname=NULL;        /* pointer to name of the file currently read from */
  outf=NULL;            /* file written to */
  glbtab.next=NULL;     /* clear global variables/constants table */
  loctab.next=NULL;     /*   "   local      "    /    "       "   */
  tagname_tab.next=NULL;/* tagname table */
  libname_tab.next=NULL;/* library table (#pragma library "..." syntax) */
  ntvindex_tab.next=NULL;

  lexptr=NULL;          /* points to the current position in "srcline" */
  inpf_org=NULL;        /* main source file */

  wq_ptr=wq_glb;        /* initialize while queue pointer */

#if !defined PAWN_LIGHT
  pc_globaldoc=NULL;
  pc_recentdoc=NULL;
  sc_makereport=false;  /* do not generate a cross-reference report */
  pc_docstring_suspended=false;
#endif
}

static char *get_extension(const char *filename)
{
  char *ptr;

  assert(filename!=NULL);
  ptr=strrchr(filename,'.');
  if (ptr!=NULL) {
    /* ignore extension on a directory or at the start of the filename */
    if (strchr(ptr,DIRSEP_CHAR)!=NULL || ptr==filename || *(ptr-1)==DIRSEP_CHAR)
      ptr=NULL;
  } /* if */
  return ptr;
}

static char *add_path_prefix(char *fullpath,size_t maxsize,const char *path,const char *filename)
{
  size_t len;

  assert(fullpath!=NULL);
  assert(path!=NULL);
  assert(filename!=NULL);
  assert(maxsize>0);
  if (fullpath!=path)
    strlcpy(fullpath,path,maxsize);
  len=strlen(fullpath); // cppcheck-suppress [uninitvar] ; CppCheck analysis is incorrect
  if (len<maxsize-1) {
    fullpath[len]=DIRSEP_CHAR;
    fullpath[len+1]='\0';
  }
  strlcat(fullpath,filename,maxsize);
  return fullpath;
}

/* set_extension
 * Set the default extension, or force an extension. To erase the
 * extension of a filename, set "extension" to an empty string.
 */
SC_FUNC void set_extension(char *filename,const char *extension,int force)
{
  char *ptr;

  assert(extension!=NULL && (*extension=='\0' || *extension=='.'));
  assert(filename!=NULL);
  ptr=get_extension(filename);
  if (force && ptr!=NULL)
    *ptr='\0';          /* set zero terminator at the position of the period */
  if (force || ptr==NULL)
    strcat(filename,extension);
}

static const char *option_value(const char *optptr)
{
  return (*(optptr+1)=='=' || *(optptr+1)==':') ? optptr+2 : optptr+1;
}

static int toggle_option(const char *optptr, int option)
{
  switch (*option_value(optptr)) {
  case '\0':
    option=!option;
    break;
  case '-':
    option=false;
    break;
  case '+':
    option=true;
    break;
  default:
    about();
  } /* switch */
  return option;
}

/* Parsing command line options is indirectly recursive: parseoptions()
 * calls parserespf() to handle options in a a response file and
 * parserespf() calls parseoptions() at its turn after having created
 * an "option list" from the contents of the file.
 */
#if !defined PAWN_LIGHT
static void parserespf(char *filename,char *oname,char *ename,char *pname,
                       char *rname, char *codepage);
#endif

static void parseoptions(int argc,char **argv,char *oname,char *ename,char *pname,
                         char *rname, char *codepage)
{
  char name[_MAX_PATH];
  const char *ptr;

  for (int arg=1; arg<argc; arg++) {
    const char *optionptr;
    bool isoption;
#   if DIRSEP_CHAR=='/'
      isoption= argv[arg][0]=='-';
#   else
      isoption= argv[arg][0]=='/' || argv[arg][0]=='-';
#   endif
    if (isoption) {
      int i;
      ptr=&argv[arg][1];
      switch (*ptr) {
      case 'A':
        i=atoi(option_value(ptr));
        if (i>0 && (i % pc_cellsize)==0)
          pc_dataalign=i;
        else
          about();
        break;
      case 'a':
        if (*(ptr+1)!='\0')
          about();
        pc_asmfile=true;        /* skip last pass of making binary file */
        if (verbosity>1)
          verbosity=1;
        break;
      case 'C':
        i=atoi(option_value(ptr));
        if ((i==16 || i==32 || i==64) && i<=PAWN_CELL_SIZE)
          pc_cellsize=i/8;
        else
          about();
        /* change default data-align too */
        if (pc_dataalign<pc_cellsize || pc_dataalign==4)
          pc_dataalign=pc_cellsize;
        break;
      case 'c':
        strlcpy(codepage,option_value(ptr),MAXCODEPAGE);  /* set name of codepage */
        break;
#if defined dos_setdrive
      case 'D':                 /* set active directory */
        ptr=option_value(ptr);
        if (ptr[1]==':')
          dos_setdrive(toupper(*ptr)-'A'+1);    /* set active drive */
        chdir(ptr);
        break;
#endif
      case 'd':
        switch (*option_value(ptr)) {
        case '0':
          sc_debug=0;
          break;
        case '1':
          sc_debug=sCHKBOUNDS;  /* assertions and bounds checking */
          break;
        case '2':
          sc_debug=sCHKBOUNDS | sSYMBOLIC;  /* also symbolic info */
          break;
        case '3':
          sc_debug=sCHKBOUNDS | sSYMBOLIC;
          pc_optimize=sOPTIMIZE_NONE;
          /* also avoid peephole optimization */
          break;
        default:
          about();
        } /* switch */
        break;
      case 'e':
        strlcpy(ename,option_value(ptr),_MAX_PATH); /* set name of error file */
        break;
      case 'i':
        /* set name of include directory */
        ptr=option_value(ptr);
        if (*ptr=='~') {
          /* this path is relative to the standard include path */
          strlcpy(name,get_path(0),sizeof name);
          i=(int)strlen(name);
          if (i>0 && name[i-1]!=DIRSEP_CHAR) {
            name[i]=DIRSEP_CHAR;
            name[i+1]='\0';
          } /* if */
          /* skip '~' and an optional directory separator following the '~' */
          ptr++;
          if (*ptr=='/' || *ptr==DIRSEP_CHAR)
            ptr++;
          strlcat(name,ptr,sizeof name);
        } else {
          /* absolute path (or relative to current directory) */
          strlcpy(name,ptr,sizeof name);
        }
        i=(int)strlen(name);
        if (i>0) {
          if (name[i-1]!=DIRSEP_CHAR) {
            name[i]=DIRSEP_CHAR;
            name[i+1]='\0';
          } /* if */
          insert_path(name);
        } /* if */
        break;
#     if defined PAWN_KEELOQ
      case 'k':
        ptr=option_value(ptr);
        while (*ptr!='\0') {
          uint64_t h;
          if ('0'<=*ptr && *ptr<='9')
            h=*ptr-'0';
          else if ('A'<=*ptr && *ptr<='F')
            h=*ptr-'A'+10;
          else if ('a'<=*ptr && *ptr<='f')
            h=*ptr-'a'+10;
          else
            h=*ptr & 0x0f;
          pc_cryptkey=(pc_cryptkey << 4) | h;
          ptr++;
        } /* while */
        break;
#     endif
      case 'l':
        if (*(ptr+1)!='\0')
          about();
        pc_listing=true;        /* skip second pass & code generation */
        break;
      case 'N':
        strlcpy(name,option_value(ptr),sizeof name);
        if ((ptr=strchr(name,'='))!=NULL && (i=atoi(ptr+1))<0) {
          *(char*)ptr='\0';
          append_constval(&ntvindex_tab,name,i,0);
          pc_addlibtable=false; /* no library table, for hard-coded natives */
        } /* if */
        break;
      case 'o':
        strlcpy(oname,option_value(ptr),_MAX_PATH); /* set name of (binary) output file */
        break;
      case 'O':
        pc_optimize=*option_value(ptr) - '0';
        if (pc_optimize<sOPTIMIZE_NONE || pc_optimize>=sOPTIMIZE_NUMBER)
          about();
        break;
      case 'p':
        strlcpy(pname,option_value(ptr),_MAX_PATH); /* set name of implicit include file */
        break;
#if !defined PAWN_LIGHT
      case 'r':
        strlcpy(rname,option_value(ptr),_MAX_PATH); /* set name of report file */
        sc_makereport=true;
        if (strlen(rname)>0)
          set_extension(rname,".xml",false);
        break;
#endif
      case 'S':
        i=atoi(option_value(ptr));
        if (i>32)
          pc_stksize=(cell)i;   /* stack size has minimum size */
        else
          about();
        break;
      case 's':
        skipinput=atoi(option_value(ptr));
        break;
      case 'T':
        /* this option was already handled on an initial scan, see setopt() */
        break;
      case 't':
        i=atoi(option_value(ptr));
        if (2<=i && i<=8)
          pc_tabsize=pc_matchedtabsize=i;
        break;
      case 'V':
        /* allow -V+, -V- and -V1234, where 1234 is the code pool size;
         * with -V+, this pool is set to 1 byte, which means "unlimited"
         */
        optionptr=option_value(ptr);
        if (isdigit(*optionptr))
          pc_overlays=atoi(optionptr);
        else
          pc_overlays=toggle_option(ptr,pc_overlays);
        /* there is a technical limit on the size of an overlay of the max. number
         * that fits in half a cell
         */
        if (pc_overlays>=((cell)1<<4*pc_cellsize))
          pc_overlays=(int)((cell)1<<4*pc_cellsize);
        break;
      case 'v':
        optionptr=option_value(ptr);
        verbosity= isdigit(*optionptr) ? atoi(optionptr) : 2;
        if (pc_asmfile && verbosity>1)
          verbosity=1;
        break;
      case 'w':
        i=(int)strtol(option_value(ptr),(char **)&ptr,10);
        if (*ptr=='-')
          pc_enablewarning(i,wDISABLE);
        else if (*ptr=='+')
          pc_enablewarning(i,wENABLE);
        else if (*ptr=='\0')
          pc_enablewarning(i,wTOGGLE);
        break;
      case 'X':
        if (*(ptr+1)=='D') {
          i=atoi(option_value(ptr+1));
          if (i>64)
            pc_amxram=(cell)i;  /* abstract machine data/stack has minimum size */
          else
            about();
        } else {
          i=atoi(option_value(ptr));
          if (i>64)
            pc_amxlimit=(cell)i;/* abstract machine has minimum size */
          else
            about();
        } /* if */
        break;
      default:                  /* wrong option */
        about();
      } /* switch */
    } else if (argv[arg][0]=='@') {
#     if !defined PAWN_LIGHT
        parserespf(&argv[arg][1],oname,ename,pname,rname,codepage);
#     endif
    } else if ((ptr=strchr(argv[arg],'='))!=NULL) {
      int i=(int)(ptr-argv[arg]);
      if (i>sNAMEMAX) {
        i=sNAMEMAX;
        error(200,argv[arg],sNAMEMAX);  /* symbol too long, truncated to sNAMEMAX chars */
      } /* if */
      strlcpy(name,argv[arg],i+1);       /* str holds symbol name */
      i=atoi(ptr+1);
      add_constant(name,i,sGLOBAL,0);
    } else {
      strlcpy(name,argv[arg],sizeof(name)-2); /* -2 because default extension is ".p" */
      set_extension(name,".p",false);
      insert_sourcefile(name);
      /* The output name is the first input name with a different extension,
       * but it is stored in a different directory
       */
      if (strlen(oname)==0) {
        if ((ptr=strrchr(name,DIRSEP_CHAR))!=NULL)
          ptr++;          /* strip path */
        else
          ptr=name;
        assert(strlen(ptr)<_MAX_PATH);
        strcpy(oname,ptr);
      } /* if */
      set_extension(oname,".asm",true);
    } /* if */
  } /* for */
#if !defined PAWN_LIGHT
  if (sc_makereport && strlen(rname)==0) {
    if (strlen(oname)==0) {
      /* no output filename is set, use the name of the first source file,
       * without the path
       */
      const char *srcname=get_sourcefile(0);
      if (srcname!=NULL) {
        assert(strlen(srcname)<_MAX_PATH);
        if ((ptr=strrchr(srcname,DIRSEP_CHAR))!=NULL)
          ptr++;          /* strip path */
        else
          ptr=srcname;
        assert(strlen(ptr)<_MAX_PATH);
        strcpy(rname,ptr);
      } /* if */
    } else {
      /* a specific output filename is set, use it for the report name too */
      strcpy(rname,oname);
    } /* if */
    set_extension(rname,".xml",true);
  } /* if */
#endif
}

#if !defined PAWN_LIGHT
static void parserespf(char *filename,char *oname,char *ename,char *pname,
                       char *rname,char *codepage)
{
#define MAX_OPTIONS     200
  FILE *fp;
  char *string, *ptr;
  char **argv;
  int argc,maxoptions;
  long size;

  if ((fp=fopen(filename,"r"))==NULL)
    error(300,filename);        /* error reading input file */
  /* load the complete file into memory */
  fseek(fp,0L,SEEK_END);
  size=ftell(fp);
  if (size<0)
    error(300,filename);        /* error reading input file */
  fseek(fp,0L,SEEK_SET);
  assert(size<INT_MAX);
  if ((string=(char *)malloc(((int)size+1)*sizeof(char)))==NULL)
    error(303);                 /* insufficient memory */
  /* fill with zeros; in MS-DOS, fread() may collapse CR/LF pairs to
   * a single '\n', so the string size may be smaller than the file
   * size. */
  memset(string,0,(int)size+1);
  fread(string,1,(int)size,fp);
  fclose(fp);
  /* remove comments in the response file */
  while ((ptr=strchr(string,'#'))!=NULL) {
    *ptr=' ';                   /* pad with spaces up to \n */
    while (*++ptr!='\n')
      *ptr=' ';
  } /* while */
  /* allocate an initial table for option pointers */
  maxoptions=2;
  if ((argv=(char**)malloc(maxoptions*sizeof(char*)))==NULL)
    error(303);                 /* insufficient memory */
  /* fill the options table */
  ptr=strtok(string," \t\r\n");
  argc=1; /* note: the routine skips argv[0], for compatibility with main() */
  while (ptr!=NULL) {
    if (argc>=maxoptions) {
      maxoptions*=2;
      char** newargv=(char**)realloc(argv,maxoptions*sizeof(char*));
      if (newargv==NULL)
        error(303);             /* insufficient memory */
      else
        argv=newargv;
    } /* if */
    argv[argc++]=ptr;
    ptr=strtok(NULL," \t\r\n");
  } /* while */
  /* parse the option table */
  parseoptions(argc,argv,oname,ename,pname,rname,codepage);
  /* free allocated memory */
  free(argv);
  free(string);
}
#endif

static void setopt(int argc,char **argv,char *oname,char *ename,char *pname,
                   char *rname,char *codepage)
{
  delete_sourcefiletable(); /* make sure it is empty */
  *oname='\0';
  *ename='\0';
  *pname='\0';
  *rname='\0';
  *codepage='\0';
  strcpy(pname,sDEF_PREFIX);

# if !defined PAWN_LIGHT
    /* first parse a "config" file with default options */
    if (sc_rootpath[0]!='\0') {
      char cfgfile[_MAX_PATH];
      const char *ptr;
      /* copy the default config file name, but keep a pointer to the location
       * of the base name
       */
      add_path_prefix(cfgfile,countof(cfgfile),sc_rootpath,"target");
      size_t pathlen=strlen(cfgfile);
      if (pathlen<countof(cfgfile)-1) {
        cfgfile[pathlen++]=DIRSEP_CHAR; /* append '/' after 'target' subdirectory */
        cfgfile[pathlen]='\0';
      }
      char *base=cfgfile+pathlen;
      assert(*base=='\0');
      strlcpy(base,"default.cfg",countof(cfgfile)-pathlen);
      /* run through the argument list to see whether a -T option is present */
      bool found=false;
      for (int i=1; i<argc && !found; i++) {
#       if DIRSEP_CHAR=='/'
          bool isoption= argv[i][0]=='-';
#       else
          bool isoption= argv[i][0]=='/' || argv[i][0]=='-';
#       endif
        if (isoption && argv[i][1]=='T') {
          found=true;
          ptr=option_value(&argv[i][1]);
          if (strchr(ptr,DIRSEP_CHAR)!=NULL)
            strlcpy(cfgfile,ptr,countof(cfgfile));      /* assume full path */
          else
            strlcpy(base,ptr,countof(cfgfile)-pathlen); /* no path, only base filename */
          ptr=strrchr(cfgfile,'.');
          if (ptr==NULL || strchr(ptr,DIRSEP_CHAR)!=NULL)
            strlcat(cfgfile,".cfg",_MAX_PATH);
        } /* if */
      } /* for */
      if (access(cfgfile,4)==0) {
        parserespf(cfgfile,oname,ename,pname,rname,codepage);
      } else if (found) {
          error(300,cfgfile);  /* config. file was explicitly specified, but cannot be read */
      } else if (sc_binpath[0]!='\0') {
        /* for compatibility, try the old name */
        add_path_prefix(cfgfile,countof(cfgfile),sc_binpath,"pawn.cfg");
        if (access(cfgfile,4)==0)
          parserespf(cfgfile,oname,ename,pname,rname,codepage);
      }
    } /* if */
# endif
  parseoptions(argc,argv,oname,ename,pname,rname,codepage);
  if (get_sourcefile(0)==NULL)
    about();
  if (pc_cryptkey!=0 && pc_cellsize<4)
    error(304,"-k","cell size < 32-bits");  /* incompatible options */
}

static void setconfig(const char *root)
{
  char path[_MAX_PATH]="";
  char *ptr;

# if defined macintosh || defined __APPLE__
    /* on OS X, use argv[0] */
    (void)root;
    getcwd(path,sizeof path);
# elif defined __WIN32__ || defined _WIN32
    (void)root;
    GetModuleFileName(NULL,path,_MAX_PATH);
# elif defined __LINUX__
    (void)root;
    /* see www.autopackage.org (now Listaller) for the BinReloc module */
    br_init(NULL);
    ptr=br_find_exe("/opt/Pawn/bin/pawncc");
    strlcpy(path,ptr,sizeof path);
    free(ptr);
# elif defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__
    (void)root;
    ptr=get_install_path();
    strlcpy(path,ptr,sizeof path);
    free(ptr);
# else
    if (root!=NULL) {
      strlcpy(path,root,sizeof path); /* path + filename (hopefully) */
    } else {
      getcwd(path,sizeof path);
      /* add a final \ or / to the path (which is stripped of later) */
      ptr=strchr(path,'\0');
      assert(ptr!=NULL);
      assert((ptr-path)<sizeof path-2);
      *ptr=DIRSEP_CHAR;
      *(ptr+1)='\0';
    } /* if */
# endif
# if defined __MSDOS__
    /* strip the options (appended to the path + filename) */
    if ((ptr=strpbrk(path," \t/"))!=NULL)
      *ptr='\0';
# endif
# if defined macintosh
    /* add a final ':' to the path */
    ptr=strchr(path,'\0');
    assert(ptr!=NULL);
    assert((ptr-path)<sizeof path-1);
    *ptr=DIRSEP_CHAR;
    *(ptr+1)='\0';
# else
    /* "path" contains path + filename, terminate just behind last \, / or : */
    if ((ptr=strrchr(path,DIRSEP_CHAR))!=NULL || (ptr=strchr(path,':'))!=NULL) {
      /* If there is no "\" or ":", the string probably does not contain the
       * path; so we just don't add it to the list in that case
       */
      *(ptr+1)='\0';
    } /* if */
# endif

  if (strlen(path)>0) {
#   if !defined PAWN_LIGHT
      assert(sizeof sc_binpath==sizeof path);
      strcpy(sc_binpath,path);
#   endif
    char *base=strchr(path,'\0')-1; /* base points to last character in the string (probably a directory separator) */
    strcat(path,"include");
    size_t len=strlen(path);
    path[len]=DIRSEP_CHAR;
    path[len+1]='\0';
    /* see if it exists */
    if (access(path,0)!=0 && *base==DIRSEP_CHAR) {
      /* There is no "include" directory below the directory where the compiler
       * is found. This typically means that the compiler is in a "bin" sub-directory
       * and the "include" is below the *parent*. So find the parent...
       */
      *base='\0';
      if ((ptr=strrchr(path,DIRSEP_CHAR))!=NULL) {
        *(ptr+1)='\0';
        strcat(path,"include");
        len=strlen(path);
        path[len]=DIRSEP_CHAR;
        path[len+1]='\0';
      } else {
        /* no parent, restore path */
        *base=DIRSEP_CHAR;
      } /* if */
    } /* if */
    insert_path(path);
    /* same for the codepage root */
#   if !defined PAWN_NO_CODEPAGE
      if (ptr!=NULL) {
        *ptr = '\0';
          if (!cp_path(path,"codepage"))
            error(309,path);        /* codepage path */
      }
#   endif
    /* also copy the root path (for the XML documentation and target host files) */
#   if !defined PAWN_LIGHT
      if ((ptr=strrchr(path,DIRSEP_CHAR))!=NULL) {
        *ptr='\0';
        strlcpy(sc_rootpath,path,sizeof sc_rootpath);
      } /* if */
#   endif
  } /* if */
}

static void setcaption(int linespacing)
{
  pc_printf("Pawn compiler %-25s Copyright (c) 1997-2026, CompuPhase\n",VERSION_STR);
  if (linespacing)
    pc_printf("\n");
}

static void about(void)
{
  if (strlen(errfname)==0) {
    setcaption(false);
    pc_printf("\t\t\t\t\tCopyright (c) 1982-1983, J.E. Hendrix\n");
    pc_printf("\t\t\t\t\tCopyright (c) 1980, Ron Cain\n\n");
    pc_printf("Usage:   pawncc <filename> [filename...] [options]\n\n");
    pc_printf("Options:\n");
    pc_printf("         -A<num>  alignment in bytes of the data segment and the stack\n");
    pc_printf("         -a       output assembler code\n");
    pc_printf("         -C<num>  cell size in bits (default=-C%d)\n",pc_cellsize*8);
    pc_printf("         -c<name> codepage name or number; e.g. 1252 for Windows Latin-1\n");
#   if defined dos_setdrive
    pc_printf("         -Dpath   active directory path\n");
#   endif
    pc_printf("         -d<num>  debugging level (default=-d%d)\n",sc_debug);
    pc_printf("             0    no symbolic information, no run-time checks\n");
    pc_printf("             1    run-time checks, no symbolic information\n");
    pc_printf("             2    full debug information and dynamic checking\n");
    pc_printf("             3    same as -d2, but implies -O0\n");
    pc_printf("         -e<name> set name of error file (quiet compile)\n");
    pc_printf("         -i<name> path for include files\n");
#   if defined PAWN_KEELOQ
    pc_printf("         -k<hex>  key for encrypted scripts\n");
#   endif
    pc_printf("         -l       create list file (preprocess only)\n");
    pc_printf("         -N<name>=<num> specify index number for native function\n");
    pc_printf("         -o<name> set base name of (P-code) output file\n");
    pc_printf("         -O<num>  optimization level (default=-O%d)\n",pc_optimize);
    pc_printf("             0    no optimization\n");
    pc_printf("             1    core instruction set (JIT-compatible)\n");
    pc_printf("             2    supplemental instruction set\n");
    pc_printf("             3    full instruction set (packed opcodes)\n");
    pc_printf("         -p<name> set the \"prefix\" file; omit name to block default prefix file\n");
#   if !defined PAWN_LIGHT
    pc_printf("         -r[name] write cross reference report to console or to specified file\n");
#   endif
    pc_printf("         -S<num>  stack/heap size in cells (default=%d)\n",(int)pc_stksize);
    pc_printf("         -s<num>  skip lines from the input file\n");
    pc_printf("         -t<num>  TAB indent size (in character positions, default=%d)\n",pc_tabsize);
    pc_printf("         -T<name> set name of the configuration file to use\n");
    pc_printf("         -V<num>  generate overlay code and instructions; set buffer size\n");
    pc_printf("         -v<num>  verbosity level; 0=quiet, 1=normal, 2=verbose (default=%d)\n",verbosity);
    pc_printf("         -w<num>  disable a specific warning by its number\n");
    pc_printf("         -X<num>  abstract machine size limit in bytes\n");
    pc_printf("         -XD<num> abstract machine data/stack size limit in bytes\n");
    pc_printf("         sym=val  define constant \"sym\" with value \"val\"\n");
    pc_printf("         sym=     define constant \"sym\" with value 0\n");
#   if defined __WIN32__ || defined _WIN32 || defined _Windows || defined __MSDOS__
    pc_printf("\nOptions may start with a dash or a slash; the options \"-d0\" and \"/d0\" are\n");
    pc_printf("equivalent.\n");
#   endif
    pc_printf("\nOptions with a value may optionally separate the value from the option letter\n");
    pc_printf("with a colon (\":\") or an equal sign (\"=\"). That is, the options \"-d0\", \"-d=0\"\n");
    pc_printf("and \"-d:0\" are all equivalent.\n");
  } /* if */
  longjmp(errbuf,3);        /* user abort */
}

static void setconstants(void)
{
  int debug;

  assert(sc_status==statIDLE);
  append_constval(&tagname_tab,"_",0,0);  /* "untagged" */
  append_constval(&tagname_tab,"bool",1,0);

  add_constant("true",1,sGLOBAL,1); /* boolean flags */
  add_constant("false",0,sGLOBAL,1);
  add_constant("EOS",0,sGLOBAL,0);  /* End Of String, or '\0' */
  add_constant("cellbits",pc_cellsize*8,sGLOBAL,0);
  add_constant("cellmax",CELLMAX,sGLOBAL,0);
  add_constant("cellmin",CELLMIN,sGLOBAL,0);
  add_constant("charbits",sCHARBITS,sGLOBAL,0);
  add_constant("charmin",0,sGLOBAL,0);
  add_constant("charmax",~(~0UL<<sCHARBITS),sGLOBAL,0);
  add_constant("ucharmax",((cell)1<<(pc_cellsize-1)*8)-1,sGLOBAL,0);

  add_constant("__Pawn",VERSION_INT,sGLOBAL,0);
  add_constant("__line",0,sGLOBAL,0);

  debug=0;
  if ((sc_debug & (sCHKBOUNDS | sSYMBOLIC))==(sCHKBOUNDS | sSYMBOLIC))
    debug=2;
  else if ((sc_debug & sCHKBOUNDS)==sCHKBOUNDS)
    debug=1;
  add_constant("debug",debug,sGLOBAL,0);

  add_constant("overlaysize",pc_overlays,sGLOBAL,0);
  append_constval(&sc_automaton_tab,"",0,0);    /* anonymous automaton */
}

#if !defined PAWN_LIGHT
/* sc_attachdocumentation()
 * appends documentation comments to the passed-in symbol, or to a global
 * string if "sym" is NULL.
 */
void sc_attachdocumentation(symbol *sym,int onlylastblock)
{
  int line;
  size_t length;
  char *doc;
  const char *txt;

  if (!sc_makereport || sc_status!=statBROWSE || sc_parsenum>0) {
    /* just clear the entire table */
    delete_docstringtable();
    return;
  } /* if */
  /* in the case of state functions, multiple documentation sections may
   * appear; we should concatenate these
   * (with forward declarations, this is also already the case, so the assertion
   * below is invalid)
   */
  // assert(sym==NULL || sym->documentation==NULL || sym->states!=NULL);

  if (pc_recentdoc!=NULL) {
    /* either copy the most recent comment block to the current symbol, or
     * append it to the global documentation
     */
    bool wrap=false;
    char *str;
    length=strlen(pc_recentdoc);
    if (onlylastblock) {
      assert(sym!=NULL);
      str=sym->documentation;
      wrap=true;
    } else {
      str=pc_globaldoc;
    } /* if */
    /* set the documentation, or append it to it */
    if (str!=NULL)
      length+=strlen(str)+1+4;  /* plus 4 for "<p/>" (see below) */
    if (wrap)
      length+=9+10;             /* size for <summary> and </summary> (see below) */
    doc=(char*)malloc((length+1)*sizeof(char));
    if (doc!=NULL) {
      char *end;
      if (str!=NULL) {
        strcpy(doc,str);
        free(str);
        strcat(doc,"<p/>");
      } else {
        *doc='\0';
      } /* if */
      end=strchr(doc,'\0');
      assert(end!=NULL);
      strcat(doc,pc_recentdoc);
      if (wrap) {
        strins(end,"<summary>",9);
        end+=9;         /* skip "<summary>" */
        while (*end!='\0' && *end!='.' && *end!='<')
          end++;
        if (*end=='.')
          end++;        /* end the summary behind the period */
        strins(end,"</summary>",10);
      } /* if */
      if (onlylastblock) {
        sym->documentation=doc;
        /* force adding the new strings (if any) to the global data */
        sym=NULL;
      } else {
        pc_globaldoc=doc;
      } /* if */
    } /* if */
    /* remove the "recent block" string */
    free(pc_recentdoc);
    pc_recentdoc=NULL;
  } /* if */

  if (onlylastblock)
    pc_docstring_suspended=false;
  else if (pc_docstring_suspended)
    return;

  /* first check the size */
  length=0;
  for (line=0; (txt=get_docstring(line))!=NULL && *txt!=sDOCSEP; line++) {
    if (length>0)
      length++;   /* count 1 extra for a separating space */
    length+=strlen(txt);
  } /* for */

  if (length>0) {
    /* allocate memory for the documentation */
    if (sym!=NULL && sym->documentation!=NULL)
      length+=strlen(sym->documentation)+1+4;   /* plus 4 for "<p/>" */
    doc=(char*)malloc((length+1)*sizeof(char));
    if (doc!=NULL) {
      /* initialize string or concatenate */
      if (sym!=NULL && sym->documentation!=NULL) {
        size_t len;
        strcpy(doc,sym->documentation);
        len=strlen(doc);
        if (len>0 && doc[len-1]!='>' || ((txt=get_docstring(0))!=NULL && *txt!=sDOCSEP && *txt!='<'))
          strcat(doc,"<p/>");
        free(sym->documentation);
        sym->documentation=NULL;
      } else {
        doc[0]='\0';
      } /* if */
      /* collect all documentation */
      while ((txt=get_docstring(0))!=NULL && *txt!=sDOCSEP) {
        if (doc[0]!='\0' && txt[0]!='<')
          strcat(doc," ");
        strcat(doc,txt);
        delete_docstring(0);
      } /* while */
      if (txt!=NULL) {
        /* also delete the separator */
        assert(*txt==sDOCSEP);
        delete_docstring(0);
      } /* if */
      if (sym!=NULL) {
        assert(sym->documentation==NULL);
        sym->documentation=doc;
      } else {
        assert(pc_recentdoc==NULL);
        pc_recentdoc=doc;
      } /* if */
    } /* if */
  } else {
    /* delete an empty separator, if present */
    if ((txt=get_docstring(0))!=NULL && *txt==sDOCSEP)
      delete_docstring(0);
  } /* if */
}

static void insert_docstring_separator(void)
{
  const char sep[2]={sDOCSEP,'\0'};
  insert_docstring(sep,1);
}
#else
# define sc_attachdocumentation(s,f)    (void)(s)
# define insert_docstring_separator()
#endif

static void plungeprefix(char *prefixname)
{

  if (strlen(prefixname)>0) {
    int ok;
    if (strchr(prefixname,DIRSEP_CHAR)==NULL) {
      /* no path, search the prefix file in the include directory */
      ok=plungefile(prefixname,false,true);
    } else {
      /* when a path is given for the prefix file, it must be absolute */
      ok=plungequalifiedfile(prefixname);
    } /* if */
    /* silently ignore a "default.inc" file that cannot be read, but give
     * an error on explicit files
     */
    if (!ok && strcmp(prefixname,sDEF_PREFIX)!=0)
      error(300,prefixname);    /* cannot read from ... (fatal error) */
  } /* if */
}

static bool getclassspec(int initialtok,bool *fpublic,bool *fstatic,bool *fstock,bool *fconst)
{
  assert(fconst!=NULL);
  assert(fstock!=NULL);
  assert(fstatic!=NULL);
  assert(fpublic!=NULL);
  *fconst=false;
  *fstock=false;
  *fstatic=false;
  *fpublic=false;
  switch (initialtok) {
  case tCONST:
    *fconst=true;
    break;
  case tSTOCK:
    *fstock=true;
    break;
  case tSTATIC:
    *fstatic=true;
    break;
  case tPUBLIC:
    *fpublic=true;
    break;
  } /* switch */

  int err=0;
  int tok;
  do {
    cell val;
    char *str;
    tok=lex(&val,&str);   /* read in (new) token */
    switch (tok) {
    case tCONST:
      if (*fconst)
        err=42;           /* invalid combination of class specifiers */
      *fconst=true;
      break;
    case tSTOCK:
      if (*fstock)
        err=42;           /* invalid combination of class specifiers */
      *fstock=true;
      break;
    case tSTATIC:
      if (*fstatic)
        err=42;           /* invalid combination of class specifiers */
      *fstatic=true;
      break;
    case tPUBLIC:
      if (*fpublic)
        err=42;           /* invalid combination of class specifiers */
      *fpublic=true;
      break;
    default:
      lexpush();
      tok=0;              /* force break out of loop */
    } /* switch */
  } while (tok && err==0);

  /* extra checks */
  if (*fstatic && *fpublic) {
    err=42;               /* invalid combination of class specifiers */
    *fstatic=*fpublic=false;
  } /* if */

  if (err)
    error(err);
  return (err==0);
}

/*  parse       - process all input text
 *
 *  At this level, only static declarations and function definitions are legal.
 */
static void parse(void)
{
  while (freading){
    /* first try whether a declaration possibly is native or public */
    bool fconst,fstock,fstatic,fpublic;
    cell val;
    char *str;
    int tok=lex(&val,&str);  /* read in (new) token */
    switch (tok) {
    case 0:
      /* ignore zero's */
      break;
    case tNEW:
    case tVAR:
      if (getclassspec(tok,&fpublic,&fstatic,&fstock,&fconst))
        declglb(NULL,0,fpublic,fstatic,fstock,fconst);
      break;
    case tSTATIC:
      /* This can be a static function or a static global variable; we know
       * which of the two as soon as we have parsed up to the point where an
       * opening paranthesis of a function would be expected. To back out after
       * deciding it was a declaration of a static variable after all, we have
       * to store the symbol name and tag.
       */
      if (getclassspec(tok,&fpublic,&fstatic,&fstock,&fconst)) {
        assert(!fpublic);
        declfuncvar(fpublic,fstatic,fstock,fconst);
      } /* if */
      break;
    case tCONST:
      decl_const(sGLOBAL);
      break;
    case tPUBLIC:
      /* This can be a public function or a public variable; see the comment
       * above (for static functions/variables) for details.
       */
      if (getclassspec(tok,&fpublic,&fstatic,&fstock,&fconst)) {
        assert(!fstatic);
        declfuncvar(fpublic,fstatic,fstock,fconst);
      } /* if */
      break;
    case tSTOCK:
      /* This can be a stock function or a stock *global*) variable; see the
       * comment above (for static functions/variables) for details.
       */
      if (getclassspec(tok,&fpublic,&fstatic,&fstock,&fconst)) {
        assert(fstock);
        declfuncvar(fpublic,fstatic,fstock,fconst);
      } /* if */
      break;
    case tEXIT:
    case tLABEL:
    case tSYMBOL:
    case tOPERATOR:
      /* at this level, if the token is tEXIT, it is probably the declaration
       * of a state exit function, not the "exit" statement
       */
      if (tok==tEXIT)
        lexsettoken(tSYMBOL,_EXITFUNC);
      lexpush();
      if (!newfunc(NULL,-1,false,false,false)) {
        error(10);              /* illegal function or declaration */
        lexclr(true);           /* drop the rest of the line */
        litidx=0;               /* drop the literal queue too */
      } /* if */
      break;
    case tNATIVE:
      funcstub(true);           /* create a dummy function */
      break;
    case tFORWARD:
      funcstub(false);
      break;
    case '}':
      error(54);                /* unmatched closing brace */
      break;
    case '{':
      error(55);                /* start of function body without function header */
      break;
    default:
      if (freading)             /* a token that ends the input is not an error by itself */
        error(10);              /* illegal function or declaration */
      lexclr(true);             /* drop the rest of the line */
      litidx=0;                 /* drop any literal arrays (strings) */
    } /* switch */
  } /* while */
  sc_attachdocumentation(NULL,false);  /* attach any trailing comment to the main documentation */
  set_deprecated(NULL,true);
}

/*  dumplits
 *
 *  Dump the literal pool (strings etc.)
 *
 *  Global references: litidx (read-only)
 */
static void dumplits(void)
{
  if (sc_status==statSKIP)
    return;
  unsigned k=0;
  while (k<litidx){
    /* should be in the data segment */
    int j=32/pc_cellsize; /* 16 values per line for 2-byte cells, 8 for 4-byte cells, etc. */
    assert(curseg==2);
    defstorage();
    while (j>0 && k<litidx){
      outval(litq[k],true,false);
      stgwrite(" ");
      k++;
      j--;
      if (j==0 || k>=litidx)
        stgwrite("\n");         /* force a newline after 10 dumps */
      /* Note: stgwrite() buffers a line until it is complete. It recognizes
       * the end of line as a sequence of "\n\0", so something like "\n\t"
       * so should not be passed to stgwrite().
       */
    } /* while */
  } /* while */
}

/*  dumpzero
 *  Dump zero's for default initial values of elements of global arrays.
 *  Elements of local arrays do not need to be dumped, because they are
 *  allocated on the stack (and the stack is cleared when creating a new
 *  variable).
 */
static void dumpzero(int count,const char *label)
{
  int i,cellsperline,firstline;

  if (sc_status==statSKIP || count<=0)
    return;
  assert(curseg==sIN_DSEG);
  cellsperline=32/pc_cellsize;
  firstline=true;
  defstorage();
  i=0;
  while (count-- > 0) {
    outval(0,true,false);
    i=(i+1) % cellsperline;
    if (i==0 || count==0) {
      if (firstline && label!=NULL) {
        stgwrite("\t; ");
        stgwrite(label);
      }
      stgwrite("\n");
      firstline=false;
    } else {
      stgwrite(" ");
    }
    if (i==0 && count>0)
      defstorage();
  } /* while */
}

static void aligndata(int numbytes)
{
  assert(numbytes % pc_cellsize == 0);  /* alignment must be a multiple of
                                         * the cell size */
  assert(numbytes!=0);

  if ((((glb_declared+litidx)*pc_cellsize) % numbytes)!=0) {
    while ((((glb_declared+litidx)*pc_cellsize) % numbytes)!=0)
      litadd(0);
  } /* if */

}

static void declfuncvar(bool fpublic,bool fstatic,bool fstock,bool fconst)
{
  int tok,tag;
  char *str;
  cell val;

  tag=pc_addtag(NULL);
  tok=lex(&val,&str);
  /* if we arrived here, this may not be a declaration of a native function
   * or variable
   */
  if (tok==tNATIVE) {
    error(42);          /* invalid combination of class specifiers */
    return;
  } /* if */

  /* at this level, if the token is tEXIT, it is probably the declaration
   * of a state exist function, not the "exit" statement
   */
  if (tok==tEXIT)
    tok=lexsettoken(tSYMBOL,_EXITFUNC);
  if (tok!=tSYMBOL && tok!=tOPERATOR) {
    lexpush();
    needtoken(tSYMBOL); /* only for the error message */
    lexclr(true);       /* drop the rest of the line */
    litidx=0;           /* drop the literal queue too */
    return;
  } /* if */
  if (tok==tOPERATOR) {
    lexpush();          /* push "operator" keyword back (for later analysis) */
    if (!newfunc(NULL,tag,fpublic,fstatic,fstock)) {
      error(10);        /* illegal function or declaration */
      lexclr(true);     /* drop the rest of the line */
      litidx=0;         /* drop the literal queue too */
    } /* if */
  } else {
    /* so tok is tSYMBOL */
    char name[sNAMEMAX+11];
    int invalidfunc;
    assert(strlen(str)<=sNAMEMAX);
    strcpy(name,str);
    /* only variables can be "const" or both "public" and "stock" */
    invalidfunc= fconst || (fpublic && fstock);
    if (invalidfunc || !newfunc(name,tag,fpublic,fstatic,fstock)) {
      /* if not a function, try a global variable */
      declglb(name,tag,fpublic,fstatic,fstock,fconst);
    } /* if */
  } /* if */
}

/** declglb() declares global variables. Global variables are stored in
 *  the DATA segment.
 *
 *  global references: glb_declared     (modified)
 *                     fcurrent         (read-only)
 *                     sc_curstates     (modified)
 *                     pc_docstring_suspended (modified)
 */
static bool declglb(const char *firstname,int firsttag,bool fpublic,bool fstatic,bool fstock,bool fconst)
{
  ucell address;
  int glb_incr;
  int dim[sDIMEN_MAX];
  constvalue *dimnames[sDIMEN_MAX];
# if !defined NDEBUG
    cell glbdecl=0;
# endif

  assert(!fpublic || !fstatic);         /* may not both be set */
  insert_docstring_separator();         /* see comment #DOCSTRING_SEPARATOR in newfunc() */
  short filenum=fcurrent;               /* save file number at the start of the declaration */
  do {
#   if !defined PAWN_LIGHT
      pc_docstring_suspended=true;      /* suspend attaching documentation to global/recent blocks */
#   endif
    cell size=1;                        /* single size (no array) */
    int numdim=0;                       /* no dimensions */
    int ident=iVARIABLE;
    char name[sNAMEMAX+1];
    int tag;
    if (firstname!=NULL) {
      assert(strlen(firstname)<countof(name));
      strlcpy(name,firstname,countof(name)); /* save symbol name */
      tag=firsttag;
      firstname=NULL;
    } else {
      tag=pc_addtag(NULL);
      cell val;
      char *str;
      if (lex(&val,&str)!=tSYMBOL) {    /* read in (new) token */
        error_symbolname(20,str);       /* invalid symbol name */
        litidx=0;                       /* clear literal pool (in case there is a string constant instead of a symbol name) */
      } /* if */
      assert(strlen(str)<countof(name));
      strlcpy(name,str,countof(name));  /* save symbol name */
    } /* if */
    int sym_line=pc_curline;            /* save line number of the symbol name, see comment below addvariable() */
    bool ispublic=fpublic;
    if (name[0]==PUBLIC_CHAR) {
      ispublic=true;                    /* implicitly public variable */
      assert(!fstatic);
    } /* if */
    while (matchtoken('[')) {
      ident=iARRAY;
      if (numdim==sDIMEN_MAX) {
        error(53);                      /* exceeding maximum number of dimensions */
        return false;
      } /* if */
      size=needsub(']',&dimnames[numdim]); /* get size; size==0 for "var[]" */
#     if INT_MAX < LONG_MAX
        if (size>INT_MAX)
          error(305);                   /* overflow, exceeding capacity */
#     endif
      dim[numdim++]=(int)size;
    } /* while */
    int usage=0;
    if (matchtoken('{')) {              /* last dimension may be specified as {} */
      ident=iARRAY;
      usage=uPACKED;
      if (numdim==sDIMEN_MAX) {
        error(53);                      /* exceeding maximum number of dimensions */
        return false;
      } /* if */
      size=needsub('}',&dimnames[numdim]);/* get size; size==0 for "var{}" */
      size=((size*sCHARBITS/8)+pc_cellsize-1) / pc_cellsize;
#     if INT_MAX < LONG_MAX
        if (size>INT_MAX)
          error(305);                   /* overflow, exceeding capacity */
#     endif
      dim[numdim++]=(int)size;
    } /* if */
    if (matchtoken('['))
      error(51);                        /* {} must be last */
    verify_array_namelist(dimnames,numdim);
    if (ispublic && ident==iARRAY)
      error(56,name);                   /* arrays cannot be public */
    assert(sc_curstates==0);
    sc_curstates=getstates(name);
    if (sc_curstates<0) {
      error(85,name);                   /* empty state list on declaration */
      sc_curstates=0;
    } else if (sc_curstates>0 && ispublic) {
      error(88,name);                   /* public variables may not have states */
      sc_curstates=0;
    } /* if */
    /* the look-ahead above may have read a string, which the lexer stores in
     * the literal queue; it cannot continue this declaration anyway
     */
    int tok=lexpeek();
    if (tok==tSTRING || tok==tPACKSTRING) {
      error(1,"=","<string literal>");  /* expected token '=' */
      skiptotoken(',',true);            /* drop the rest of the declaration */
      litidx=0;                         /* and the literals that came with it */
    } /* if */
    symbol *sym=findconst(name);
    if (sym==NULL) {
      sym=findglb(name,sSTATEVAR);
      /* if a global variable without states is found and this declaration has
       * states, the declaration is okay
       */
      if (sym!=NULL && sym->states==NULL && sc_curstates>0)
        sym=NULL;               /* set to NULL, we found the global variable */
      if (sc_curstates>0 && findglb(name,sGLOBAL)!=NULL)
        error(233,name);        /* state variable shadows a global variable */
    } /* if */
    /* we have either:
     * a) not found a matching variable (or rejected it, because it was a shadow)
     * b) found a global variable and we were looking for that global variable
     * c) found a state variable in the automaton that we were looking for
     */
    assert(sym==NULL
           || sym->states==NULL && sc_curstates==0
           || sym->states!=NULL && sym->next!=NULL && sym->states->next->id==sc_curstates);
    /* a state variable may only have a single id in its list (so either this
     * variable has no states, or it has a single list)
     */
    assert(sym==NULL || sym->states==NULL || sym->states->next->next==NULL);
    /* it is okay for the (global) variable to exist, as long as it belongs to
     * a different automaton
     */
    if (sym!=NULL && (sym->usage & uDEFINE)!=0)
      error(21,name);                   /* symbol already defined */
    /* if this variable is never used (which can be detected only in the
     * second stage), shut off code generation
     */
    cell cidx=0;        /* only to avoid a compiler warning */
    if (sc_status==statWRITE && sym!=NULL && (sym->usage & (uREAD | uWRITTEN))==0) {
      sc_status=statSKIP;
      cidx=code_idx;
#     if !defined NDEBUG
        glbdecl=glb_declared;
#     endif
    } /* if */
    begdseg();          /* real (initialized) data in data segment */
    assert(litidx==0);  /* literal queue should be empty */
    if (pc_pendingflags & flgALIGNDATA) {
      litidx=0;
      aligndata(pc_dataalign);
      dumplits();       /* dump the literal queue */
      litidx=0;         /* global initial data is dumped, so restart at zero */
    } /* if */
    pc_pendingflags=0;
    assert(litidx==0);  /* literal queue should be empty (again) */
    initials(ident,usage,tag,&size,dim,numdim,dimnames);/* stores values in the literal queue */
    if ((unsigned)size<litidx) {
      int oldstatus=sc_status;
      if (sc_status==statSKIP)
        sc_status=statWRITE;
      errorset(sRESET); /* this is an important error; it should not be gobbled */
      error(10);        /* invalid declaration */
      litidx=0;
      sc_status=oldstatus;
    }
    if (numdim==1)
      dim[0]=(int)size;
    /* before dumping the initial values (or zeros) check whether this variable
     * overlaps another
     */
    if (sc_curstates>0) {
      if (litidx!=0)
        error(89,name); /* state variables may not be initialized */
      /* find an appropriate address for the state variable */
      /* assume that it cannot be found */
      address=pc_cellsize*glb_declared;
      glb_incr=(int)size;
      /* use a memory map in which every cell occupies one bit */
      unsigned char *map;
      if (glb_declared>0 && (map=(unsigned char*)malloc((size_t)(glb_declared+7)/8))!=NULL) {
        int fsa=state_getfsa(sc_curstates);
        symbol *sweep;
        cell sweepsize,addr;
        memset(map,0,(size_t)(glb_declared+7)/8);
        assert(fsa>=0);
        /* fill in all variables belonging to this automaton */
        for (sweep=glbtab.next; sweep!=NULL; sweep=sweep->next) {
          if (sweep->parent!=NULL || sweep->states==NULL || sweep==sym)
            continue;   /* hierarchical type, or no states, or same as this variable */
          if (sweep->ident!=iVARIABLE && sweep->ident!=iARRAY)
            continue;   /* a function or a constant */
          if ((sweep->usage & uDEFINE)==0)
            continue;   /* undefined variable, ignore */
          if (fsa!=state_getfsa(sweep->states->next->id))
            continue;   /* wrong automaton */
          /* when arrived here, this is a global variable, with states and
           * belonging to the same automaton as the variable we are declaring
           */
          sweepsize=(sweep->ident==iVARIABLE) ? 1 : array_totalsize(sweep);
          assert(sweep->addr % pc_cellsize == 0);
          addr=sweep->addr/pc_cellsize;
          /* mark this address range */
          while (sweepsize-->0) {
            map[addr/8] |= (unsigned char)(1U<<(addr % 8));
            addr++;
          } /* while */
        } /* for */
        /* go over it again, clearing any ranges that have conflicts */
        for (sweep=glbtab.next; sweep!=NULL; sweep=sweep->next) {
          if (sweep->parent!=NULL || sweep->states==NULL || sweep==sym)
            continue;   /* hierarchical type, or no states, or same as this variable */
          if (sweep->ident!=iVARIABLE && sweep->ident!=iARRAY)
            continue;   /* a function or a constant */
          if ((sweep->usage & uDEFINE)==0)
            continue;   /* undefined variable, ignore */
          if (fsa!=state_getfsa(sweep->states->next->id))
            continue;   /* wrong automaton */
          /* when arrived here, this is a global variable, with states and
           * belonging to the same automaton as the variable we are declaring
           */
          /* if the lists of states of the existing variable and the new
           * variable have a non-empty intersection, this is not a suitable
           * overlap point -> wipe the address range
           */
          if (state_conflict_id(sc_curstates,sweep->states->next->id)) {
            sweepsize=(sweep->ident==iVARIABLE) ? 1 : array_totalsize(sweep);
            assert(sweep->addr % pc_cellsize == 0);
            addr=sweep->addr/pc_cellsize;
            /* mark this address range */
            while (sweepsize-->0) {
              map[addr/8] &= (unsigned char)(~(1U<<(addr % 8)));
              addr++;
            } /* while */
          } /* if */
        } /* for */
        /* now walk through the map and find a starting point that is big enough */
        sweepsize=0;
        for (addr=0; addr<glb_declared; addr++) {
          if ((map[addr/8] & (1U<<(addr % 8)))==0)
            continue;
          for (sweepsize=addr+1; sweepsize<glb_declared; sweepsize++) {
            if ((map[sweepsize/8] & (1U<<(sweepsize % 8)))==0)
              break;    /* zero bit found, skip this range */
            if (sweepsize-addr>=size)
              break;    /* fitting range found, no need to search further */
          } /* for */
          if (sweepsize-addr>=size)
            break;      /* fitting range found, no need to search further */
          addr=sweepsize;
        } /* for */
        free(map);
        if (sweepsize-addr>=size) {
          address=pc_cellsize*addr;     /* fitting range found, set it */
          glb_incr=0;
        } /* if */
      } /* if */
    } else {
      address=pc_cellsize*glb_declared;
      glb_incr=(int)size;
    } /* if */
    if (address==(ucell)pc_cellsize*glb_declared) {
      dumplits();       /* dump the literal queue */
      dumpzero((int)size-litidx,(litidx==0) ? name : NULL);
    } /* if */
    litidx=0;
    if (strlen(name)==0)
      continue;
    if (sym==NULL) {    /* define only if not yet defined */
      sym=addvariable(name,address,ident,sGLOBAL,0,tag,dim,dimnames,numdim,usage);
      sc_attachdocumentation(sym,true);/* attach any documenation to the variable */
      if (sc_curstates>0)
        attachstatelist(sym,sc_curstates);
    } else {            /* if declared but not yet defined, adjust the variable's address */
      int idx;
      assert(sym->states==NULL && sc_curstates==0
             || sym->states->next!=NULL && sym->states->next->id==sc_curstates && sym->states->next->next==NULL);
      sym->addr=address;
      sym->codeaddr=code_idx;
      sym->usage|=uDEFINE;
      for (idx=0; idx<numdim; idx++) {
        if (dimnames[idx]!=NULL) {
          delete_consttable(dimnames[idx]);
          free(dimnames[idx]);
        } /* if */
      } /* for */
    } /* if */
    assert(sym!=NULL);
    sc_curstates=0;             /* clear 'current state' ID when declaration is done */
    sym->lnumber=sym_line;      /* attempt to parse array dimensions may have overread symbol definition, so reset it */
    if (ispublic)
      sym->usage|=uPUBLIC;
    if (fconst)
      sym->usage|=uCONST;
    if (fstock)
      sym->usage|=uSTOCK;
    if (fstatic)
      sym->fvisible=filenum;
    if (sc_status==statSKIP) {
      sc_status=statWRITE;
      code_idx=cidx;
      assert(glb_declared==glbdecl);
    } else {
      glb_declared+=glb_incr;   /* add total number of cells (if added to the end) */
    } /* if */
  } while (matchtoken(','));    /* more? */
  return (needtoken(tTERM)!=0); /* if not comma, must be semicolon or end-of-line */
}

/** declloc() declares local variables.
 *
 *  Local variables have the scope of the compound block, but "static" variables
 *  persist after the function ends. Static locals are in the DATA block and
 *  must be initialized with constant data. Standard locals are relative to the
 *  STACK; these variables can be initialized with an expression.
 *
 *  global references: lcl_declared (modified)
 *                     undefined_vars (read-only)
 */
static bool declloc(bool fstatic)
{
  value lval={0};
  unsigned cur_lit=0;
  int dim[sDIMEN_MAX];
  constvalue *dimnames[sDIMEN_MAX];

  bool fconst= (matchtoken(tCONST)==1);
  do {
    int ident=iVARIABLE;
    cell size=1;
    int tag=pc_addtag(NULL);
    if (!needtoken(tSYMBOL)) {
      lexclr(true);                     /* drop the rest of the line... */
      litidx=0;                         /* ...clear literal pool... */
      return false;                     /* ...and quit */
    } /* if */
    char *str;
    tokeninfo(NULL,&str);
    char name[sNAMEMAX+1];
    assert(strlen(str)<countof(name));
    strlcpy(name,str,countof(name));    /* save symbol name */
    if (name[0]==PUBLIC_CHAR)
      error(56,name);                   /* local variables cannot be public */
    symbol *sym=findlcl(name);
    /* Note: block locals may be named identical to locals at higher
     * compound blocks (as with standard C); so we must check (and add)
     * the "nesting level" of local variables to verify the
     * multi-definition of symbols.
     */
    if (sym!=NULL && sym->block==pc_blocklevel)
      error(21,name);                   /* symbol already defined */
    /* Although valid, a local variable whose name is equal to that
     * of a global variable or to that of a local variable at a lower
     * level might indicate a bug.
     */
    if ((sym!=NULL && sym->block!=pc_blocklevel || findglb(name,sGLOBAL)!=NULL) && !undefined_vars)
      error(219,name);                  /* variable shadows another symbol (only give the warning when there are no errors) */
    int sym_line=pc_curline;            /* save line number of the symbol name, see comment below addvariable() */
    int numdim=0;
    while (matchtoken('[')) {
      ident=iARRAY;
      if (numdim == sDIMEN_MAX) {
        error(53);                      /* exceeding maximum number of dimensions */
        return false;
      } /* if */
      size=needsub(']',&dimnames[numdim]);/* get size; size==0 for "var[]" */
#     if INT_MAX < LONG_MAX
        if (size > INT_MAX)
          error(305);                   /* overflow, exceeding capacity */
#     endif
      dim[numdim++]=(int)size;
    } /* while */
    int usage=0;
    if (matchtoken('{')) {              /* final dimension may also be specified with {} */
      ident=iARRAY;
      usage=uPACKED;
      if (numdim == sDIMEN_MAX) {
        error(53);                      /* exceeding maximum number of dimensions */
        return false;
      } /* if */
      size=needsub('}',&dimnames[numdim]);/* get size; size==0 for "var{}" */
      size=((size*sCHARBITS/8)+pc_cellsize-1) / pc_cellsize;
#     if INT_MAX < LONG_MAX
        if (size > INT_MAX)
          error(305);                   /* overflow, exceeding capacity */
#     endif
      dim[numdim++]=(int)size;
    } /* if */
    if (matchtoken('['))
      error(51);                /* {} must be last */
    verify_array_namelist(dimnames,numdim);
    if (getstates(name))
      error(88,name);           /* local variables may not have states */
    if (ident==iARRAY || fstatic) {
      if (pc_pendingflags & flgALIGNDATA)
        aligndata(pc_dataalign);
      pc_pendingflags=0;
      cur_lit=litidx;           /* save current index in the literal table */
      initials(ident,usage,tag,&size,dim,numdim,dimnames);
      if (size==0) {
        skiptotoken(tTERM,false);
        return false;           /* error message already given */
      }
      if (numdim==1)
        dim[0]=(int)size;
    } /* if */
    /* reserve memory (on the stack) for the variable */
    unsigned staging_start;
    if (fstatic) {
      /* write zeros for uninitialized fields */
      while (litidx<cur_lit+size)
        litadd(0);
      sym=addvariable(name,(cur_lit+glb_declared)*pc_cellsize,ident,sSTATIC,pc_blocklevel,
                      tag,dim,dimnames,numdim,usage);
    } else {
      lcl_declared+=(int)size;  /* variables are put on stack, adjust "declared" */
      sym=addvariable(name,-lcl_declared*pc_cellsize,ident,sLOCAL,pc_blocklevel,
                      tag,dim,dimnames,numdim,usage);
      if (ident==iVARIABLE) {
        assert(!staging);
        stgset(true);           /* start stage-buffering */
        assert(stgidx==0);
        staging_start=stgidx;
      } /* if */
      markexpr(sLDECL,name,-lcl_declared*pc_cellsize); /* mark for better optimization */
      modstk(-(int)size*pc_cellsize);
      assert(curfunc!=NULL);
      assert((curfunc->usage & uNATIVE)==0);
      if (curfunc->x.stacksize<lcl_declared+1)
        curfunc->x.stacksize=(long)lcl_declared+1;  /* +1 for PROC opcode */
    } /* if */
    /* now that we have reserved memory for the variable, we can proceed
     * to initialize it
     */
    assert(sym!=NULL);          /* we declared it, it must be there */
    sym->lnumber=sym_line;      /* attempt to parse array dimensions may have overread symbol definition, so reset it */
    if (fconst)
      sym->usage|=uCONST;
    if (!fstatic) {             /* static variables already initialized */
      if (ident==iVARIABLE) {
        /* simple variable, also supports initialization */
        int ctag = tag;         /* set to "tag" by default */
        int explicit_init=false;/* is the variable explicitly initialized? */
        if (matchtoken('=')) {
          doexpr(false,false,false,false,&ctag,NULL,true,0);
          explicit_init=true;
        } else {
          ldconst(0,sPRI);      /* uninitialized variable, set to zero */
        } /* if */
        /* now try to save the value (still in PRI) in the variable */
        lval.sym=sym;
        lval.ident=iVARIABLE;
        lval.constval=0;
        lval.tag=tag;
        check_userop(NULL,ctag,lval.tag,2,NULL,&ctag);
        store(&lval);
        markexpr(sEXPR,NULL,0); /* full expression ends after the store */
        assert(staging);        /* end staging phase (optimize expression) */
        stgout(staging_start);
        stgset(false);
        if (!matchtag(tag,ctag,true))
          error(213);           /* tag mismatch */
        /* if the variable was not explicitly initialized, reset the
         * "uWRITTEN" flag that store() set */
        if (!explicit_init)
          sym->usage &= ~uWRITTEN;
      } else {
        /* an array */
        assert(cur_lit<=litidx);
        assert(size>0 && size>=sym->dim.array.length);
        assert(numdim>1 || size==sym->dim.array.length);
        /* final literal values that are zero make no sense to put in the literal
         * pool, because values get zero-initialized anyway; we check for this,
         * because users often explicitly initialize strings to ""
         */
        while (litidx>cur_lit && litq[litidx-1]==0)
          litidx--;
        /* if the array is not completely filled, set all values to zero first */
        if (litidx-cur_lit<(size_t)size && size<CELLMAX)
          fillarray(sym,size*pc_cellsize,0);
        if (cur_lit<litidx) {
          /* check whether all elements in the array are set to the same value;
           * if it is, more compact code can be generated
           */
          cell first=litq[cur_lit];
          unsigned i;
          for (i=cur_lit; i<litidx && litq[i]==first; i++)
            /* nothing */;
          if (i==litidx) {
            /* all values are the same */
            if (litidx-cur_lit==1) {
              /* make another exception if there is only a single cell to fill */
              lval.sym=sym;
              lval.ident=iVARIABLE;
              ldconst(first,sPRI);
              store(&lval);
            } else {
              fillarray(sym,(litidx-cur_lit)*pc_cellsize,first);
            }
            litidx=cur_lit;     /* reset literal queue */
          } else {
            /* copy the literals to the array, but first check whether the
             * array literal can be merged with an earlier one
             */
            cell src_addr=cur_lit+glb_declared;
            cell src_size=litidx-cur_lit;
            assert(litidx>=cur_lit);
            if (litarray_find(&src_addr,cur_lit,src_size)) {
              litidx=cur_lit;   /* reset literal queue */
            } else {
              /* save this literal array as a candidate for a future merge */
              litarray_add(glb_declared,cur_lit,src_size);
            } /* if */
            ldconst(src_addr*pc_cellsize,sPRI); /* PRI = source */
            address(sym,sALT);                  /* ALT = dest */
            sym->usage &= ~uREAD; /* clear this flag that address() implicitly sets */
            memcopy(src_size*pc_cellsize);
          } /* if */
          markusage(sym,uWRITTEN);
        } /* if */
      } /* if */
    } /* if */
  } while (matchtoken(','));    /* more? */
  return (needtoken(tTERM)!=0); /* if not comma, must be semicolon/end-of-line */
}

/* this function returns the maximum value for a cell in case of an error
 * (invalid dimension).
 */
static cell calc_arraysize(int dim[],int numdim,int cur)
{
# if INT_MAX > 0x7fffffffL
#   define ARRAY_MAX 0xffffffffLu  /* array is max. 4 GiB */
# else
#   define ARRAY_MAX INT_MAX       /* array is max. 2 GiB on 32-bit */
# endif
  cell subsize;
  ucell newsize;

  /* the return value is in cells, not bytes */
  assert(cur>=0 && cur<=numdim);
  if (cur==numdim)
    return 0;
  subsize=calc_arraysize(dim,numdim,cur+1);
  newsize=dim[cur]+dim[cur]*subsize;
  if (newsize==0)
    return 0;
  if ((ucell)subsize>=ARRAY_MAX || newsize>=ARRAY_MAX || newsize<(ucell)subsize
      || newsize*pc_cellsize>=ARRAY_MAX)
    return CELLMAX;
  return newsize;
# undef ARRAY_MAX
}

static cell adjust_indirectiontables(int dim[],int numdim,int cur,cell increment,
                                     int startlit,constvalue *lastdim,int *skipdim)
{
static int base;
  cell accum;

  assert(cur>=0 && cur<numdim);
  assert(increment>=0);
  assert(cur>0 && startlit==-1 || startlit>=0 && (unsigned)startlit<=litidx);
  if (cur==0)
    base=startlit;
  if (cur==numdim-1)
    return 0;
  /* 2 or more dimensions left, fill in an indirection vector */
  assert(dim[cur]>0);
  if (dim[cur+1]>0) {
    for (int d=0; d<dim[cur]; d++)
      litq[base++]=(dim[cur]+d*(dim[cur+1]-1)+increment)*pc_cellsize;
    accum=(cell)(dim[cur]*(dim[cur+1]-1));
  } else {
    /* final dimension is variable length */
    constvalue *ld;
    assert(dim[cur+1]==0);
    assert(lastdim!=NULL);
    assert(skipdim!=NULL);
    accum=0;
    /* skip the final dimension sizes for all earlier major dimensions */
    int d;
    for (d=0,ld=lastdim->next; d<*skipdim; d++,ld=ld->next) {
      assert(ld!=NULL);
    } /* for */
    for (d=0; d<dim[cur]; d++) {
      assert(ld!=NULL);
      assert(strtol(ld->name,NULL,16)==d);
      litq[base++]=(dim[cur]+accum+increment)*pc_cellsize;
      accum+=ld->value-1;
      *skipdim+=1;
      ld=ld->next;
    } /* for */
  } /* if */
  /* create the indirection tables for the lower level */
  if (cur+2<numdim) {   /* are there at least 2 dimensions below this one? */
    increment+=(dim[cur]-1)*dim[cur+1]; /* this many indirection tables follow */
    for (int d=0; d<dim[cur]; d++)
      increment+=adjust_indirectiontables(dim,numdim,cur+1,increment,-1,lastdim,skipdim);
  } /* if */
  return accum;
}

/*  initials
 *
 *  Initialize global objects and local arrays.
 *
 *  ident   a combination of the kind of variable (array, simple variable)
 *  usage   flags (only the uPACKED flag is used)
 *  size    array cells (count), if 0 on input, the routine counts the number of elements
 *  tag     required tagname for the initial value
 *
 *  Global references: litidx (modified)
 */
static void initials(int ident,int usage,int tag,cell *size,int dim[],int numdim,
                     constvalue *dimnames[])
{
  int ctag;
  int curlit=(int)litidx;

  if (!matchtoken('=')) {
    assert(ident!=iARRAY || numdim>0);
    if (ident==iARRAY) {
      cell tablesize;
      int i;
      assert(numdim>0 && numdim<=sDIMEN_MAX);
      for (i=0; i<numdim; i++) {
        if (dim[i]==0) {
          /* any of array size is zero which is senseless (note: this *does* make
           * sense in the case of a iREFARRAY, which is a function parameter)
           */
          error(9);         /* array has zero length -> invalid size */
          return;
        }
      } /* if */
      *size=calc_arraysize(dim,numdim,0);
      if (*size==CELLMAX) {
        error(9);       /* array is too big -> invalid size */
        return;
      } /* if */
      /* first reserve space for the indirection vectors of the array, then
       * adjust it to contain the proper values
       * (do not use dumpzero(), as it bypasses the literal queue)
       */
      for (tablesize=calc_arraysize(dim,numdim-1,0); tablesize>0; tablesize--)
        litadd(0);
      if (dim[numdim-1]!=0)     /* error 9 has already been given */
        adjust_indirectiontables(dim,numdim,0,0,curlit,NULL,NULL);
    } /* if */
    return;
  } /* if */

  if (ident==iVARIABLE) {
    assert(*size==1);
    init(ident,usage,&ctag,NULL,NULL,NULL);
    if (!matchtag(tag,ctag,true))
      error(213);       /* tag mismatch */
  } else {
    assert(numdim>0);
    if (numdim==1) {
      *size=initvector(ident,usage,tag,dim[0],false,dimnames[0],NULL);
    } else {
      bool errorfound=false;
      int counteddim[sDIMEN_MAX];
      int idx;
      constvalue lastdim={0};   /* sizes of the final dimension */
      int skipdim=0;

      if (dim[numdim-1]!=0)
        *size=calc_arraysize(dim,numdim,0); /* calc. full size, if known */
      /* initialize the sub-arrays */
      memset(counteddim,0,sizeof counteddim);
      initarray(ident,usage,tag,dim,numdim,0,curlit,counteddim,&lastdim,dimnames,&errorfound);
      /* check the specified array dimensions with the initialler counts */
      for (idx=0; idx<numdim-1; idx++) {
        if (dim[idx]==0) {
          dim[idx]=counteddim[idx];
        } else if (counteddim[idx]<dim[idx]) {
          error(52);            /* array is not fully initialized */
          errorfound=true;
        } else if (counteddim[idx]>dim[idx]) {
          error(18);            /* initialization data exceeds declared size */
          errorfound=true;
        } /* if */
      } /* for */
      if (numdim>1 && dim[numdim-1]==0 && !errorfound) {
        /* also look whether, by any chance, all "counted" final dimensions are
         * the same value; if so, we can store this
         */
        constvalue *ld;
        int count=0,match,total,d;
        for (ld=lastdim.next; ld!=NULL; ld=ld->next) {
          assert(strtol(ld->name,NULL,16)==count % dim[numdim-2]);  /* index is stored in the name, it should match the sequence */
          if (count==0)
            match=(int)ld->value;
          else if (match!=ld->value)
            break;
          count++;
        } /* for */
        total=dim[numdim-2];
        for (d=numdim-3; d>=0; d--)
          total*=dim[d];
        if (count>0 && count==total)
          dim[numdim-1]=match;
      } /* if */
      /* after all arrays have been initalized, we know the (major) dimensions
       * of the array and we can properly adjust the indirection vectors
       */
      if (!errorfound)
        adjust_indirectiontables(dim,numdim,0,0,curlit,&lastdim,&skipdim);
      delete_consttable(&lastdim);  /* clear list of minor dimension sizes */
    } /* if */
  } /* if */

  if (*size==0)
    *size=litidx-curlit;        /* number of elements defined */
}

static cell initarray(int ident,int usage,int tag,int dim[],int numdim,int cur,
                      int startlit,int counteddim[],constvalue *lastdim,
                      constvalue *dimnames[],bool *errorfound)
{
  cell dsize,totalsize;
  int idx,abortparse;

  assert(cur>=0 && cur<numdim);
  assert(startlit>=0);
  assert(cur+2<=numdim);    /* there must be 2 dimensions or more to do */
  assert(errorfound!=NULL && *errorfound==false);
  /* check for a quick exit */
  if (matchtoken(']')) {
    lexpush();
    return 0;
  } /* if */
  totalsize=0;
  if (!needtoken('[')) {
    if (numdim>1 && lexpeek()!='{') {
      errorset(sRESET);     /* force additional error, because this is the likely cause */
      error(52);            /* array is not fully initialized */
    }
    startlit=0;             /* because literal pool is cleared on a mismatch in needtoken() */
  }
  for (idx=0,abortparse=false; !abortparse; idx++) {
    /* We need to store the offset to the newly detected sub-array into the
     * indirection table; i.e. this table needs to be expanded and updated.
     * In the current design, the indirection vectors for a multi-dimensional
     * array are adjusted after parsing all initiallers. Hence, it is only
     * necessary at this point to reserve space for an extra cell in the
     * indirection vector.
     */
    litinsert(0,startlit);
    if (cur+2<numdim) {
      dsize=initarray(ident,usage,tag,dim,numdim,cur+1,startlit,counteddim,
                      lastdim,dimnames,errorfound);
    } else {
      dsize=initvector(ident,usage,tag,dim[cur+1],true,dimnames[cur+1],errorfound);
      /* The final dimension may be variable length. We need to save the
       * lengths of the final dimensions in order to set the indirection
       * vectors for the next-to-last dimension.
       */
      append_constval(lastdim,celltohex(idx),dsize,0);
    } /* if */
    /* In a declaration like:
     *    new a[2][2] = [
     *                    [1, 2],
     *                    [3, 4],
     *                  ]
     * the final trailing comma (after the "4}") makes this loop attempt to
     * parse one more initialization vector, but initvector() (and a recursive
     * call to initarray()) quits with a size of zero while not setting
     * "errorfound". Then, we must exit this loop without incrementing the
     * dimension count, and also undo the insertion of the last entry in the
     * indirection table.
     */
    if (dsize==0 && !*errorfound) {
      litremove(startlit);
      break;
    }
    if (dim[cur]!=0 && idx>=dim[cur]) {
      assert(dsize>0 || *errorfound);
      error(18);            /* initialization data exceeds array size */
      break;                /* avoid incrementing "idx" */
    } /* if */
    totalsize+=dsize;
    if (*errorfound || !matchtoken(','))
      abortparse=true;
  } /* for */
  needtoken(']');
  assert(counteddim!=NULL);
  if (counteddim[cur]>0) {
    if (idx<counteddim[cur])
      error(52);                /* array is not fully initialized */
    else if (idx>counteddim[cur])
      error(18);                /* initialization data exceeds declared size */
  } /* if */
  counteddim[cur]=idx;

  return totalsize+dim[cur];    /* size of sub-arrays + indirection vector */
}

/** initvector() initializeS a single dimensional array.
 *  \return The size in elements (cells for standard arrays, bytes for byte
 *          arrays).
 */
static cell initvector(int ident,short usage,int tag,cell size,int fillzero,
                       constvalue *dimnames,bool *errorfound)
{
  cell prev1=0,prev2=0;
  int ellips=false;
  unsigned curlit=litidx;
  int ctag;
  int match=0;
  int packcount=-1;
  cell packitem=0;

  assert(ident==iARRAY || ident==iREFARRAY);
  if (matchtoken('[')) {
    match=']';
    if (usage & uPACKED)
      error(229);
  } else if (matchtoken('{')) {
    match='}';
    packcount=0;        /* this enables item packing */
    if ((usage & uPACKED)==0)
      error(229);
  } /* if */
  if (match!=0) {
    constvalue *field=(dimnames!=NULL) ? dimnames->next : NULL;
    if (match=='}' && field!=NULL)
      error(51);        /* error (only unpacked arrays can use symbolic indices) */
    do {
      unsigned fieldlit=litidx;
      if (matchtoken(match)) {  /* to allow for trailing ',' after the initialization */
        lexpush();
        break;
      } /* if */
      if ((ellips=matchtoken(tELLIPS))!=0)
        break;
      /* for symbolic subscripts, allow another level of braces ("[...]" or "{...}") */
      int matchbrace=0;         /* preset */
      short fieldusage=usage;
      if (field!=NULL) {
        if (matchtoken('['))
          matchbrace=']';
        else if (matchtoken('{'))
          matchbrace='}';
        fieldusage=field->usage;
      } /* if */
      int ftag=-1;
      for ( ;; ) {
        prev2=prev1;
        prev1=init(ident,fieldusage,&ctag,errorfound,&packcount,&packitem);
        if (ftag!=-1 && ftag!=ctag)
          error(213);     /* tag mismatch */
        ftag=ctag;
        if (!matchbrace)
          break;
        if ((ellips=matchtoken(tELLIPS))!=0)
          break;
        if (!matchtoken(',')) {
          needtoken(matchbrace);
          break;
        } /* for */
      } /* for */
      /* if this array array field is a pseudo-array, fill the "field" up with
       * zeros, and toggle the tag
       */
      if (dimnames!=NULL && (field==NULL || field->name[0]=='\0'))
        error(227);             /* more initiallers than array fields */
      if (field!=NULL && field->name[0]!='\0') {
        cell step,val;
        assert(field->next!=NULL);
        cell fsize=(field->next->value - field->value);
        assert(fieldlit<litidx || packcount>0);
        if (litidx-fieldlit>(unsigned)fsize)
          error(228);           /* length of initialler exceeds size of the array field */
        if (ellips) {
          val=prev1;
          step=prev1-prev2;
        } else {
          step=0;
          val=0;                /* fill up with zeros */
        } /* if */
        for (unsigned i=litidx-fieldlit; i<fsize; i++) {
          if (packcount>=0) {
            assert(packcount<pc_cellsize);
            do {
              val+=step;
              packcount+=1;
              packitem|=(cell)(((ucell)val & 0xff) << ((pc_cellsize-packcount)*sCHARBITS));
            } while (packcount<pc_cellsize);
            litadd(packitem);    /* store collected values in literal table */
            packitem=0;
            packcount=0;
          } else {
            val+=step;
            litadd(val);
          } /* if */
        } /* for */
        ftag=field->index;
        field=field->next;
      } else {
        ftag=tag;
      } /* if */
      if (!matchtag(ftag,ctag,true))
        error(213);             /* tag mismatch */
    } while (matchtoken(','));
    if (packcount!=0 && match=='}' && !ellips)
      litadd(packitem);         /* store final collected values */
    needtoken(match);
  } else if (matchtoken(']')) {
    /* this may be caused by a trailing comma in a declaration of a
     * multi-dimensional array
     */
    lexpush();                  /* push back for later analysis */
    size=0;                     /* avoid zero filling */
    assert(!ellips);
  } else {
    init(ident,usage,&ctag,errorfound,NULL,NULL);
    if (!matchtag(tag,ctag,true))
      error(213);               /* tagname mismatch */
  } /* if */
  /* fill up the literal queue with a series */
  if (ellips) {
    cell step=((litidx-curlit)>1 || packcount>1) ? prev1-prev2 : (cell)0;
    if (size==0 || (litidx-curlit)==0 && packcount<=0)
      error(41);                /* invalid ellipsis, array size unknown */
    else if ((litidx-curlit)==(unsigned)size)
      error(18);                /* initialisation data exceeds declared size */
    while ((litidx-curlit)<(unsigned)size) {
      if (packcount>=0) {
        assert(packcount<pc_cellsize);
        do {
          prev1+=step;
          packcount+=1;
          packitem|=(cell)(((ucell)prev1 & 0xff) << ((pc_cellsize-packcount)*sCHARBITS));
        } while (packcount<pc_cellsize);
        litadd(packitem);    /* store collected values in literal table */
        packitem=0;
        packcount=0;
      } else {
        prev1+=step;
        litadd(prev1);
      } /* if */
    } /* while */
  } /* if */
  if (fillzero && size>0) {
    while ((litidx-curlit)<(unsigned)size)
      litadd(0);
  } /* if */
  if (size==0) {
    size=litidx-curlit;                       /* number of elements defined */
  } else if (litidx-curlit>(unsigned)size) {  /* e.g. "myvar[3]=[1,2,3,4];" */
    error(18);                  /* initialisation data exceeds declared size */
    litidx=size+curlit;         /* avoid overflow in memory moves */
  } /* if */
  return size;
}

/*  init
 *
 *  Evaluate one initializer.
 */
static cell init(int ident,int usage,int *tag,bool *errorfound,int *packcount,cell *packitem)
{
  cell val;
  char *str;
  int tok;
  unsigned curlit=litidx;
  cell i = 0;

  tok=lex(&val,&str);
  if (tok!=tSTRING && tok!=tPACKSTRING)
    lexpush();
  if (tok==tSTRING || tok==tPACKSTRING) {
    /* lex() automatically stores strings in the literal table (and
     * increases "litidx")
     */
    if (ident==iVARIABLE) {
      error(6);                 /* must be assigned to an array */
      if (litidx>curlit)
        litidx=curlit+1;        /* reset literal queue */
    } /* if */
    if (tok==tSTRING && (usage & uPACKED)!=0 || tok==tPACKSTRING && (usage & uPACKED)==0)
      error(229);
    *tag=0;
  } else if (constexpression(&i,tag,NULL)) {
    if (packcount!=NULL && *packcount>=0) {
      assert(packitem!=NULL);
      assert(*packcount<pc_cellsize);
      if ((ucell)i>=(ucell)(1U<<sCHARBITS))
        error(43,(long)i);      /* constant exceeds range */
      *packcount+=1;
      *packitem|=(cell)(((ucell)i & 0xff) << ((pc_cellsize-*packcount)*sCHARBITS));
      if (*packcount==pc_cellsize) {
        litadd(*packitem);      /* store collected values in literal table */
        *packitem=0;
        *packcount=0;
      } /* if */
    } else {
      litadd(i);                /* store expression result in literal table */
    } /* if */
  } else {
    if (errorfound!=NULL)
      *errorfound=true;
  } /* if */
  return i;
}

/*  needsub
 *
 *  Get required array size
 */
static cell needsub(char match,constvalue **namelist)
{
  cell val;

  if (namelist!=NULL)
    *namelist=NULL;
  if (matchtoken(match))    /* we have already seen "[" (or "{") */
    return 0;               /* zero size (like "char msg[]") */
  int tag=pc_addtag(NULL);
  if (namelist!=NULL && matchtoken(tSYMLABEL)) {
    /* this is a name list */
    char *str;
    cell index=0;
    cell size;
    if (match!=']') {
      error(51);  /* named array fields only allowed for non-packed arrays */
      /* ignore everything until a matching token is found */
      do {
        lex(&val,&str);
      } while (freading && !matchtoken(match));
      return 0;
    } /* if */
    if ((*namelist=(constvalue*)malloc(sizeof(constvalue)))==NULL)
      error(303);
    memset(*namelist,0,sizeof(constvalue));
    for ( ;; ) {
      /* add the name that was already parsed */
      tokeninfo(NULL,&str);
      assert(strlen(str)>0 && alphanum(str[0]));
      constvalue *field=append_constval(*namelist,str,index,tag);
      /* get the size (parse optional '[' and '{') */
      if (matchtoken('[')) {
        size=needsub(']',NULL); /* get size; size==0 for "var[]" */
      } else if (matchtoken('{')) {
        field->usage=uPACKED;
        size=needsub('}',NULL); /* get size; size==0 for "var{}" */
        size=((size*sCHARBITS/8)+pc_cellsize-1) / pc_cellsize;
      } else {
        size=1;
      } /* if */
#     if INT_MAX < LONG_MAX
        if (size>INT_MAX)
          error(305);           /* overflow, exceeding capacity */
#     endif
      if (matchtoken('[') || matchtoken('{'))
        error(53);              /* exceeding maximum number of dimensions */
      if (size<=0)
        error(9);               /* invalid array size */
      index+=size;
      /* see whether more names follow */
      if (!matchtoken(','))
        break;
      if (matchtoken(match)) {
        /* closing brace/bracket following the comma, push back but no error */
        lexpush();
        break;
      } /* if */
      tag=pc_addtag(NULL);
      if (!needtoken(tSYMLABEL)) {
        skiptotoken(match,true);/* invalid symbol, skip to ']' or '}' */
        break;
      }
    } /* for */
    /* add a sentinel, so that we know the size of the last symbol */
    append_constval(*namelist,"",index,0);
    val=index;
  } else {
    symbol *sym;
    int exprtag;
    constexpression(&val,&exprtag,&sym);/* get value (must be constant expression) */
    if (tag==0)
      tag=exprtag;            /* copy expression tag */
    if (val<0 || is_rational(tag,false)) {
      error(9);               /* negative array size is invalid; so is rational value */
      val=1;                  /* assume a valid array size */
    } /* if */
  } /* if */
  needtoken(match);

  return val;               /* return array size */
}

static void verify_array_namelist(constvalue *namelist[], int numdim)
{
  int dim;
  cell size;
  constvalue *field;

  /* only in the final dimension, an array may have fields with a size */
  for (dim=0; dim<numdim-1; dim++) {
    if (namelist[dim]==NULL)
      continue;
    assert(namelist[dim]->next!=NULL);
    for (field=namelist[dim]->next; field!=NULL && strlen(field->name)>0; field=field->next) {
      assert(field->next!=NULL);  /* there must be a sentinel */
      size=(field->next->value - field->value);
      if (size!=1)
        error(93);  /* array fields with a size may only appear in the final dimension */
    } /* for */
  } /* for */
}

/*  decl_const  - declare a single constant, or a list of enumerated
 *  constants
 */
static void decl_const(int scope)
{
  char constname[sNAMEMAX+1];
  cell val;
  char *str;
  int tag,exprtag;
  int symbolline;
  symbol *sym;

  insert_docstring_separator();         /* see comment #DOCSTRING_SEPARATOR in newfunc() */
  do {
#   if !defined PAWN_LIGHT
      pc_docstring_suspended=true;      /* suspend attaching documentation to global/recent blocks */
#   endif
    int defaulttag=pc_addtag(NULL);
    cell defaultval=0;
    int enumerate=matchtoken('{') ? ++pc_enumsequence : 0;
    int first=1;
    do {
      if (enumerate!=0 && matchtoken('}')) { /* quick exit if '}' follows ',' */
        lexpush();
        break;
      } /* if */
      if (enumerate==0 || (tag=pc_addtag(NULL))==0)
        tag=defaulttag;
      if (lex(&val,&str)!=tSYMBOL)      /* read in (new) token */
        error_symbolname(20,str);       /* invalid symbol name */
      symbolline=pc_curline;            /* save line where symbol was found */
      strcpy(constname,str);            /* save symbol name */
      val=defaultval+1;                 /* set default value and tag for next enumeration field */
      exprtag=tag;
      assert(enumerate!=0 || first);    /* when not enumerating, each constant is the "first" of the list */
      if (enumerate) {
        if (matchtoken('='))
          constexpression(&val,&exprtag,NULL);/* get value (optional, except for the first) */
        else if (first)
          error(91,constname);          /* first constant in a list must be initialized */
      } else {
        needtoken('=');
        constexpression(&val,&exprtag,NULL); /* get value */
      }
      first=0;
      defaultval=val;
      if (exprtag!=0 && !matchtag(tag,exprtag,false)) {
        error_onpos(-1,symbolline,213); /* tagname mismatch */
      } /* if */
      /* add_constant() checks for duplicate definitions */
      sym=add_constant(constname,val,scope,tag);
      if (sym!=NULL) {
        sc_attachdocumentation(sym,true); /* attach any documenation to the constant */
        sym->x.enumlist=enumerate;
      } /* if */
    } while (enumerate!=0 && matchtoken(tSEPARATOR) && freading);
    if (enumerate!=0)
      needtoken('}');                   /* terminates the constant list */
  } while (matchtoken(','));            /* more? */
  needtoken(tTERM);
  matchtoken(';');                      /* eat an optional ; */
}

static int getstates(const char *symname)
{
  char fsaname[sNAMEMAX+1],statename[sNAMEMAX+1];
  char *str;
  const constvalue *automaton;
  int fsa;
  int *list;
  int count,listsize,state_id;

  if (!matchtoken('<'))
    return 0;
  if (matchtoken('>'))
    return FALLBACK;    /* special construct: all other states (fall-back) */

  count=0;
  listsize=0;
  list=NULL;
  automaton=NULL;
  fsa=-1;

  do {
    const constvalue *state;
    int islabel;
    if ((islabel=matchtoken(tLABEL))==0 && !needtoken(tSYMBOL))
      break;
    tokeninfo(NULL,&str);
    assert(strlen(str)<sizeof fsaname);
    strcpy(fsaname,str);  /* assume this is the name of the automaton */
    if (islabel || matchtoken(':')) {
      /* token is an automaton name, add the name and get a new token */
      if (!needtoken(tSYMBOL))
        break;
      tokeninfo(NULL,&str);
      assert(strlen(str)<sizeof statename);
      strcpy(statename,str);
    } else {
      /* the token was the state name (part of an anynymous automaton) */
      assert(strlen(fsaname)<sizeof statename);
      strcpy(statename,fsaname);
      fsaname[0]='\0';
    } /* if */
    if (fsa<0 || fsaname[0]!='\0') {
      automaton=automaton_add(fsaname);
      assert(automaton!=NULL);
      if (fsa>=0 && automaton->index!=fsa)
        error(83,symname); /* multiple automatons for a single function/variable */
      fsa=automaton->index;
    } /* if */
    state=state_add(statename,fsa);
    /* add this state to the state combination list (it will be attached to the
     * automaton later) */
    state_buildlist(&list,&listsize,&count,(int)state->value);
  } while (matchtoken(','));
  needtoken('>');

  if (count>0) {
    assert(automaton!=NULL);
    assert(fsa>=0);
    state_id=state_addlist(list,count,fsa);
    assert(state_id>0);
  } else {
    /* error is already given */
    state_id=0;
  } /* if */
  if (list!=NULL)
    free(list);

  return state_id;
}

static statelist *attachstatelist(symbol *sym,int state_id)
{
  assert(sym!=NULL);
  if (sym->ident==iFUNCTN && (sym->usage & uDEFINE)!=0 && (sym->states==NULL || state_id==0))
    error(21,sym->name); /* function already defined, either without states or the current definition has no states */

  if (state_id!=0) {
    /* add the state list id */
    statelist *stateptr;
    if (sym->states==NULL) {
      if ((sym->states=(statelist*)malloc(sizeof(statelist)))==NULL)
        error(303);             /* insufficient memory (fatal error) */
      memset(sym->states,0,sizeof(statelist));
    } /* if */
    /* see whether the id already exists (add new state only if it does not
     * yet exist
     */
    assert(sym->states!=NULL);
    for (stateptr=sym->states->next; stateptr!=NULL && stateptr->id!=state_id; stateptr=stateptr->next)
      /* nothing */;
    assert(state_id<=SHRT_MAX);
    if (stateptr==NULL)
      stateptr=append_statelist(sym->states,state_id,0,code_idx);
    else if (stateptr->addr==0)
      stateptr->addr=code_idx;
    else
      error(84,sym->name);      /* state conflict */
    /* also check for another conflicting situation: a fallback function
     * without any states
     */
    if (state_id==FALLBACK && sc_status!=statBROWSE) {
      /* in the second round, all states should have been collected */
      bool validstate=false;
      for (statelist *stlist=sym->states->next; stlist && !validstate; stlist=stlist->next)
        validstate= (stlist->id!=FALLBACK);
      if (!validstate)
        stateptr=NULL;
    } /* if */
    return stateptr;
  } /* if */

  return NULL;
}

/*
 *  Finds a function in the global symbol table or creates a new entry.
 *  It does some basic processing and error checking.
 */
SC_FUNC symbol *fetchfunc(const char *name,int tag)
{
  symbol *sym;

  assert(name!=NULL);
  if ((sym=findglb(name,sGLOBAL))!=NULL) {/* already in symbol table? */
    if (sym->ident!=iFUNCTN) {
      error(21,name);                     /* yes, but not as a function */
      return NULL;                        /* make sure the old symbol is not damaged */
    } else if ((sym->usage & uNATIVE)!=0) {
      error(21,name);                     /* yes, and it is a native */
    } /* if */
    assert(sym->scope==sGLOBAL);
    if ((sym->usage & uPROTOTYPED)!=0 && sym->tag!=tag)
      error(25);                          /* mismatch from earlier prototype */
    if ((sym->usage & uDEFINE)==0) {
      /* as long as the function stays undefined, update the address and the tag */
      if (sym->states==NULL)
        sym->addr=code_idx;
      sym->tag=tag;
    } /* if */
  } else {
    /* don't set the "uDEFINE" flag; it may be a prototype */
    sym=addsym(name,code_idx,iFUNCTN,sGLOBAL,0,tag,0);
    assert(sym!=NULL);          /* fatal error 103 must be given on error */
    /* assume no arguments */
    sym->dim.arglist=(arginfo*)calloc(1, sizeof(arginfo));
    sym->dim.arglist[0].ident=0;
    /* set library ID to NULL (only for native functions) */
    sym->x.lib=NULL;
    /* set the required stack size to zero (only for non-native functions) */
    sym->x.stacksize=1;         /* 1 for PROC opcode */
  } /* if */
  assert(sym!=NULL);
  if (get_deprecated()!=NULL) {
    sym->flags|=flgDEPRECATED;
    if (sc_status==statWRITE) {
      if (sym->documentation!=NULL)
        free(sym->documentation);
      sym->documentation=strdup(get_deprecated());
    } /* if */
    set_deprecated(NULL,false);
  } /* if */
  if (strcmp(name,_MAINFUNC)==0 || strcmp(name,_STARTFUNC)==0)
    sym->flags|=flgENTRYPOINT;

  return sym;
}

/* This routine adds symbolic information for each argument.
 */
static void define_args(void)
{
  symbol *sym;

  /* At this point, no local variables have been declared. All
   * local symbols are function arguments.
   */
  sym=loctab.next;
  while (sym!=NULL) {
    assert(sym->ident!=iLABEL);
    assert(sym->scope==sLOCAL);
    markexpr(sLDECL,sym->name,sym->addr); /* mark for better optimization */
    sym=sym->next;
  } /* while */
}

static int operatorname(char *name)
{
  int opertok;
  char *str;
  cell val;

  assert(name!=NULL);

  /* check the operator */
  opertok=lex(&val,&str);
  switch (opertok) {
  case '+':
  case '-':
  case '*':
  case '/':
  case '%':
  case '>':
  case '<':
  case '!':
  case '~':
  case '=':
    name[0]=(char)opertok;
    name[1]='\0';
    break;
  case tINC:
    strcpy(name,"++");
    break;
  case tDEC:
    strcpy(name,"--");
    break;
  case tlEQ:
    strcpy(name,"==");
    break;
  case tlNE:
    strcpy(name,"!=");
    break;
  case tlLE:
    strcpy(name,"<=");
    break;
  case tlGE:
    strcpy(name,">=");
    break;
  default:
    name[0]='\0';
    error(7);           /* operator cannot be redefined (or bad operator name) */
    return 0;
  } /* switch */

  return opertok;
}

/** operatoradjust() changes the name of the function based on the operator
 *  and the tag names of its parameters. It is used for operator overloading.
 *
 *  \param opertok      The token (typically a character) for the operator.
 *  \param sym          [in/out] The symbol that has been created for the
 *                      function (but without its symbol name set).
 *  \param opername     [in] The current (dummy) name of the function.
 *  \param resulttag    The tag of the function result. It is only used for the
 *                      '=' operator.
 */
static int operatoradjust(int opertok,symbol *sym,const char *opername,int resulttag)
{
  if (opertok==0)
    return true;

  assert(sym!=NULL && sym->ident==iFUNCTN && sym->dim.arglist!=NULL);
  /* count arguments and save (first two) tags */
  int count=0;
  int tags[2]={0,0};
  arginfo *arg;
  while (arg=&sym->dim.arglist[count], arg->ident!=0) {
    if (count<2) {
      if (arg->numtags>1)
        error(65,arg->name);  /* function parameter may only have a single tag */
      else if (arg->numtags==1)
        tags[count]=arg->tags[0];
    } /* if */
    if (opertok=='~' && count==0) {
      if (arg->ident!=iREFARRAY)
        error(73,arg->name);  /* parameter must be an array */
    } else {
      if (arg->ident!=iVARIABLE)
        error(66,arg->name);  /* parameter may not be a reference */
    } /* if */
    if (arg->hasdefault)
      error(59,arg->name);    /* parameters of an operator may not have a default value */
    count++;
  } /* while */

  /* for '!', '++' and '--', count must be 1
   * for '-', count may be 1 or 2
   * for '=', count must be 1, and the resulttag is also important
   * for all other (binary) operators and the special '~' operator, count must be 2
   */
  switch (opertok) {
  case '!':
  case '=':
  case tINC:
  case tDEC:
    if (count!=1) {
      error(62);      /* number or placement of the operands does not fit the operator */
      return false;   /* no tags to build the operator name from */
    } /* if */
    break;
  case '-':
    if (count!=1 && count!=2) {
      error(62);      /* number or placement of the operands does not fit the operator */
      return false;
    } /* if */
    break;
  default:
    if (count!=2) {
      error(62);      /* number or placement of the operands does not fit the operator */
      return false;
    } /* if */
  } /* switch */

  if (tags[0]==0 && (opertok!='=' && tags[1]==0 || opertok=='=' && resulttag==0))
    error(64);        /* cannot change predefined operators */

  /* change the operator name */
  assert(opername!=NULL && strlen(opername)>0);
  char tmpname[sNAMEMAX+1];
  operator_symname(tmpname,countof(tmpname),opername,tags[0],tags[1],count,resulttag);
  symbol *oldsym=findglb(tmpname,sGLOBAL);
  if (oldsym) {
    if ((oldsym->usage & uDEFINE)!=0) {
      char errname[2*sNAMEMAX+16];
      funcdisplayname(errname,countof(errname),tmpname);
      error(21,errname);        /* symbol already defined */
    } /* if */
    sym->usage|=oldsym->usage;  /* copy flags from the previous definition */
    sym->index=oldsym->index;   /* copy overlay index */
    /* copy refer list of old symbol to new symbol */
    for (int i=0; i<oldsym->numrefers; i++)
      if (oldsym->refer[i]!=NULL)
        refer_symbol(sym,oldsym->refer[i]);
    oldsym->numrefers=0;
    /* if any other symbol references the old symbol, change it to the new symbol */
    for (symbol *scan=glbtab.next; scan!=NULL; scan=scan->next) {
      if (scan==oldsym)
        continue;
      assert(scan->refer!=NULL);
      for (int i=0; i<scan->numrefers; i++)
        if (scan->refer[i]==oldsym)
          scan->refer[i]=sym;
    } /* for */
    delete_symbol(&glbtab,oldsym);
  } /* if */
  strcpy(sym->name,tmpname);
  sym->hash=namehash(sym->name);/* calculate new hash */

  /* operators should return a value, except the '~' operator */
  if (opertok!='~')
    sym->usage |= uRETVALUE;

  return true;
}

static int check_operatortag(int opertok,int resulttag,char *opername)
{
  assert(opername!=NULL && strlen(opername)>0);
  switch (opertok) {
  case '!':
  case '<':
  case '>':
  case tlEQ:
  case tlNE:
  case tlLE:
  case tlGE:
    if (resulttag!=pc_addtag("bool")) {
      error(63,opername,"bool:"); /* operator X requires a "bool:" result tag */
      return false;
    } /* if */
    break;
  case '~':
    if (resulttag!=0) {
      error(63,opername,"_:");    /* operator "~" requires a "_:" result tag */
      return false;
    } /* if */
    break;
  } /* switch */
  return true;
}

static char *tag2str(char *dest,size_t destsize,int tag)
{
  tag &= TAGMASK;
  assert(tag>=0);
  snprintf(dest,destsize,"0%x",tag);
  return isdigit(dest[1]) ? &dest[1] : dest;
}

SC_FUNC char *operator_symname(char *symname,size_t symnamesz,const char *opername,int tag1,int tag2,int numtags,int resulttag)
{
  char tagstr1[10], tagstr2[10];
  int opertok;

  assert(numtags>=1 && numtags<=2);
  opertok= (opername[1]=='\0') ? opername[0] : 0;
  if (opertok=='=')
    snprintf(symname,symnamesz,"%s%s%s",tag2str(tagstr1,countof(tagstr1),resulttag),
             opername,tag2str(tagstr2,countof(tagstr2),tag1));
  else if (numtags==1 || opertok=='~')
    snprintf(symname,symnamesz,"%s%s",opername,tag2str(tagstr1,countof(tagstr1),tag1));
  else
    snprintf(symname,symnamesz,"%s%s%s",tag2str(tagstr1,countof(tagstr1),tag1),
             opername,tag2str(tagstr2,countof(tagstr2),tag2));
  return symname;
}

static int parse_funcname(const char *fname,int *tag1,int *tag2,char *opname,size_t opnamesz)
{
  const char *ptr;
  char *name;
  int unary;

  /* tags are only positive, so if the function name starts with a '-',
   * the operator is an unary '-' or '--' operator.
   */
  if (*fname=='-') {
    *tag1=0;
    unary=true;
    ptr=fname;
  } else {
    *tag1=(int)strtol(fname,(char**)&ptr,16);
    unary= ptr==fname;  /* unary operator if it doesn't start with a tag name */
  } /* if */
  assert(!unary || *tag1==0);
  assert(*ptr!='\0');
  name=opname;
  while (*ptr!='\0' && !isdigit(*ptr) && (size_t)(name-opname)<opnamesz-1)
    *name++ = *ptr++;
  *name='\0';
  *tag2=(int)strtol(ptr,NULL,16);
  return unary;
}

static constvalue *find_tag_byval(int tag)
{
  constvalue *tagsym;
  tagsym=find_constval_byval(&tagname_tab,tag & ~PUBLICTAG);
  if (tagsym==NULL)
    tagsym=find_constval_byval(&tagname_tab,tag | PUBLICTAG);
  return tagsym;
}

SC_FUNC char *funcdisplayname(char *dest,size_t destsz,const char *funcname)
{
  int tags[2];
  char opname[sNAMEMAX+1];
  constvalue *tagsym[2];
  int unary;

  assert(funcname!=NULL);
  if (isalpha(*funcname) || *funcname=='_' || *funcname==PUBLIC_CHAR || *funcname=='\0') {
    if (dest!=funcname)
      strlcpy(dest,funcname,destsz);
    return dest;
  } /* if */

  unary=parse_funcname(funcname,&tags[0],&tags[1],opname,countof(opname));
  tagsym[1]=find_tag_byval(tags[1]);
  assert(tagsym[1]!=NULL);
  if (unary) {
    snprintf(dest,destsz,"operator%s(%s:)",opname,tagsym[1]->name);
  } else {
    tagsym[0]=find_tag_byval(tags[0]);
    assert(tagsym[0]!=NULL);
    /* special case: the assignment operator has the return value as the 2nd tag */
    if (opname[0]=='=' && opname[1]=='\0')
      snprintf(dest,destsz,"%s:operator%s(%s:)",tagsym[0]->name,opname,tagsym[1]->name);
    else
      snprintf(dest,destsz,"operator%s(%s:,%s:)",opname,tagsym[0]->name,tagsym[1]->name);
  } /* if */
  return dest;
}

static void funcstub(int fnative)
{
  char *str;
  cell val,size;
  char symbolname[sNAMEMAX+1];
  int dim[sDIMEN_MAX];
  int numdim;
  constvalue *dimnames[sDIMEN_MAX];
  symbol *sym;

  int opertok=0;
  lastst=0;
  litidx=0;                     /* clear the literal pool */
  assert(loctab.next==NULL);    /* local symbol table should be empty */

  int tag=pc_addtag(NULL);      /* get the tag of the return value */
  int tok=lex(&val,&str);
  bool fpublic=(tok==tPUBLIC) || (tok==tSYMBOL && str[0]==PUBLIC_CHAR);
  if (fnative) {
    if (fpublic || tok==tSTOCK || tok==tSTATIC || tok==tSYMBOL && *str==PUBLIC_CHAR)
      error(42);                /* invalid combination of class specifiers */
  } else {
    if (tok==tPUBLIC || tok==tSTOCK || tok==tSTATIC)
      tok=lex(&val,&str);
  } /* if */

  if (tok==tOPERATOR) {
    opertok=operatorname(symbolname);
    if (opertok==0)
      return;                   /* error message already given */
    check_operatortag(opertok,tag,symbolname);
  } else {
    if (tok!=tSYMBOL && freading) {
      error(10);                /* illegal function or declaration */
      return;
    } /* if */
    strcpy(symbolname,str);
  } /* if */

  numdim=0;
  while (matchtoken('[')) {
    /* the function returns an array, get this tag for the index and the array
     * dimensions
     */
    if (numdim==sDIMEN_MAX) {
      error(53);                /* exceeding maximum number of dimensions */
      return;
    } /* if */
    size=needsub(']',&dimnames[numdim]);/* get size; size==0 for "var[]" */
    if (size==0)
      error(9);                 /* invalid array size */
#   if INT_MAX < LONG_MAX
      if (size>INT_MAX)
        error(305);             /* overflow, exceeding capacity */
#   endif
    dim[numdim++]=(int)size;
  } /* while */
  int usage=0;
  if (matchtoken('{')) {        /* handle {} for the last dimension, too */
    if (numdim==sDIMEN_MAX) {
      error(53);                /* exceeding maximum number of dimensions */
      return;
    } /* if */
    usage=uPACKED;
    size=needsub('}',&dimnames[numdim]);/* get size; size==0 for "var{}" */
    if (size==0)
      error(9);                 /* invalid array size */
    size=((size*sCHARBITS/8)+pc_cellsize-1) / pc_cellsize;
#   if INT_MAX < LONG_MAX
      if (size>INT_MAX)
        error(305);             /* overflow, exceeding capacity */
#   endif
    dim[numdim++]=(int)size;
  } /* if */
  if (matchtoken('['))
    error(51);                  /* {} must be last */
  verify_array_namelist(dimnames,numdim);

  needtoken('(');               /* only functions may be native/forward */
  sym=fetchfunc(symbolname,tag);/* get a pointer to the function entry */
  if (sym==NULL)
    return;
  if ((sym->flags & flgENTRYPOINT)!=0)
    fpublic=false;
  if (fnative) {
    sym->usage=uNATIVE | uRETVALUE | uDEFINE | (sym->usage & uPROTOTYPED);
    sym->x.lib=curlibrary;
  } else {
    if (fpublic)
      sym->usage|=uPUBLIC;
    else
      sym->usage&=~uPUBLIC;     /* forward declaration is decisive */
  } /* if */
  sym->usage|=uFORWARD;

  declparams(sym,false);
  /* "declargs()" found the ")" */
  sc_attachdocumentation(sym,true);  /* attach any documenation to the function */
  if (!operatoradjust(opertok,sym,symbolname,tag))
    sym->usage &= ~uDEFINE;

  if (getstates(symbolname)!=0) {
    if (fnative || opertok!=0)
      error(82);                /* native functions and operators may not have states */
    else
      error(231);               /* ignoring state specifications on forward declarations */
  } /* if */

  /* for a native operator, also need to specify an "exported" function name;
   * for a native function, this is optional
   */
  if (fnative) {
    constvalue *ntvidx;
    if (opertok!=0) {
      needtoken('=');
      lexpush();        /* push back, for matchtoken() to retrieve again */
    } /* if */
    if (matchtoken('=')) {
      /* allow number or symbol */
      if (matchtoken(tSYMBOL)) {
        tokeninfo(NULL,&str);
        insert_alias(sym->name,str);
        /* see whether the aliased name has a preset index */
        if ((ntvidx=find_constval(&ntvindex_tab,str,-1))!=NULL) {
          assert(ntvidx->value<0);
          sym->index=(int)ntvidx->value;
        } /* if */
      } else {
        constexpression(&val,NULL,NULL);
        sym->index=(int)val;
        /* At the moment, I have assumed that this syntax is only valid if
         * val < 0. To properly mix "normal" native functions and indexed
         * native functions, one should use negative indices anyway.
         * Special code for a negative index in sym->addr exists in SC4.C
         * (ffcall()) and in SC6.C (the loops for counting the number of native
         * variables and for writing them).
         */
      } /* if */
    } else {
      /* no explicit name/index for this native function, see whether one
       * was preset
       */
      if ((ntvidx=find_constval(&ntvindex_tab,sym->name,-1))!=NULL) {
        assert(ntvidx->value<0);
        sym->index=(int)ntvidx->value;
      } else if (ntvindex_tab.next!=NULL) {
        /* no predefined index for this native function, but the native index
         * table is not empty: mixing predefined and automatic indiced -> give
         * a warning
         */
        error(232,sym->name);
      } /* if */
    } /* if */
  } /* if */
  needtoken(tTERM);

  /* attach the array to the function symbol */
  if (numdim>0) {
    assert(sym!=NULL);
    symbol *sub=addvariable(symbolname,0,iREFARRAY,sGLOBAL,0,tag,dim,dimnames,numdim,usage);
    sub->parent=sym;
  } /* if */

  litidx=0;                     /* clear the literal pool */
  delete_symbols(&loctab,0,true,true);/* clear local variables queue */
}

/*  newfunc    - begin a function
 *
 *  This routine is called from "parse" and tries to make a function
 *  out of the following text
 *
 *  Global references: litidx   (modified)
 *                     lastst   (modified)
 *                     rettype  (modified)
 *                     curfunc  (modified)
 *                     glb_declared (modified)
 *                     pc_pendingflags (modified)
 */
static int newfunc(const char *firstname,int firsttag,int fpublic,int fstatic,int stock)
{
  symbol *sym;
  int tag;
  char symbolname[sNAMEMAX+1];
  char *str;
  cell val;
  short filenum;

  assert(litidx==0);    /* literal queue should be empty */
  litidx=0;             /* clear the literal pool (should already be empty) */
  int opertok=0;
  lastst=0;             /* no statement yet */
  cell cidx=0;
  cell glbdecl=0;
  assert(loctab.next==NULL);    /* local symbol table should be empty */
  filenum=fcurrent;     /* save file number at the start of the declaration */

  if (firstname!=NULL) {
    assert(strlen(firstname)<=sNAMEMAX);
    strcpy(symbolname,firstname); /* save symbol name */
    tag=firsttag;
  } else {
    tag= (firsttag>=0) ? firsttag : pc_addtag(NULL);
    int tok=lex(&val,&str);
    assert(!fpublic);
    if (tok==tNATIVE || tok==tPUBLIC && stock)
      error(42);                /* invalid combination of class specifiers */
    if (tok==tOPERATOR) {
      opertok=operatorname(symbolname);
      if (opertok==0)
        return true;            /* error message already given */
      check_operatortag(opertok,tag,symbolname);
    } else {
      if (tok!=tSYMBOL && freading) {
        error_suggest(20,str,iFUNCTN);  /* invalid symbol name */
        return false;
      } /* if */
      assert(strlen(str)<=sNAMEMAX);
      strcpy(symbolname,str);
    } /* if */
  } /* if */
  /* check whether this is a function or a variable declaration */
  if (!matchtoken('('))
    return false;
  /* so it is a function, proceed */
  if (symbolname[0]==PUBLIC_CHAR) {
    fpublic=true;               /* implicitly public function */
    if (stock)
      error(42);                /* invalid combination of class specifiers */
  } /* if */
  sym=fetchfunc(symbolname,tag);/* get a pointer to the function entry */
  if (sym==NULL || (sym->usage & uNATIVE)!=0)
    return true;                /* it was recognized as a function declaration, but not as a valid one */
  sym->lnumber=pc_curline;      /* save line & file at which the function is defined */
  sym->fnumber=fcurrent;
  /* attach a preceding comment to the function's documentation */
  sc_attachdocumentation(sym,true);
  if ((sym->usage & uPUBLIC)!=0 && !fpublic) {
    fpublic=true;               /* earlier (forward) declaration said this is a public function */
    error(25);                  /* function definition does not match prototype */
  } /* if */
  if (fpublic && (sym->flags & flgENTRYPOINT)==0)
    sym->usage|=uPUBLIC;
  if (fstatic)
    sym->fvisible=filenum;
  /* if the function was used before being declared, and it has a tag for the
   * result, add a third pass (as second "browse" parse) because the function
   * result may have been used with user-defined operators, which have now
   * been incorrectly flagged (as the return tag was unknown at the time of
   * the call)
   */
  if ((sym->usage & (uPROTOTYPED | uREAD))==uREAD && sym->tag!=0) {
    int curstatus=sc_status;
    sc_status=statWRITE;  /* temporarily set status to WRITE, so the warning isn't blocked */
    error(208,symbolname);
    sc_status=curstatus;
    sc_reparse=true;      /* must add another pass to "initial scan" phase */
  } /* if */
  if ((sym->flags & flgENTRYPOINT)!=0) {
    /* verify that there are not two functions with an entry point flag */
    int count=0;
    symbol *symloop;
    for (symloop=glbtab.next; symloop!=NULL; symloop=symloop->next)
      if (symloop->parent==NULL && symloop->ident==iFUNCTN && (symloop->flags & flgENTRYPOINT)!=0)
        count+=1;
    assert(count>=1);
    if (count>1)
      error(95);
  } else {
    /* we want public functions to be explicitly prototyped, as they are called
     * from the outside (the exception are main() and @start(), which are
     * implicitly forward declared)
     */
    if (fpublic && (sym->usage & uFORWARD)==0)
      error(235,symbolname);
  }
  /* declare all parameters */
  int paramcnt=declparams(sym,true);
  /* for functions with a variable argument list, create intrinsic local variables */
  vararg_locals(sym);
  int opererror=!operatoradjust(opertok,sym,symbolname,tag);
  if ((sym->flags & flgENTRYPOINT)!=0
      || strcmp(symbolname,_ENTRYFUNC)==0 || strcmp(symbolname,_EXITFUNC)==0)
  {
    if (paramcnt>0)
      error(5);         /* main(), @start(), entry() and exit() functions may not have any parameters */
    sym->usage|=uREAD;  /* these entry & exit points for programs or states are implicitly "used" */
  } /* if */
  int state_id=getstates(symbolname);
  if (state_id!=0 && (opertok!=0 || (sym->flags & flgENTRYPOINT)!=0))
    error(82);          /* operators may not have states, main() may neither */
  statelist *stlist=attachstatelist(sym,state_id);
  if (stlist==NULL && state_id!=0) {
    error(85,sym->name);/* no states are defined for this function */
    state_id=0;
  }
  /* "declargs()" found the ")"; if a ";" appears after this, it was a
   * prototype */
  if (matchtoken(';')) {
    sym->usage|=uFORWARD;
    error(218);         /* old style prototypes used with optional semicolons */
    delete_symbols(&loctab,0,true,true);  /* prototype is done; forget everything */
    return true;
  } /* if */
  /* so it is not a prototype, proceed */
  /* if this is a function that is not referred to (this can only be detected
   * in the second stage), shut code generation off */
  if (sc_status==statWRITE && (sym->usage & uREAD)==0 && !fpublic) {
    sc_status=statSKIP;
    cidx=code_idx;
    glbdecl=glb_declared;
  } /* if */
  if ((sym->flags & flgDEPRECATED)!=0 && (sym->usage & uPUBLIC)!=0) {
    char *ptr= (sym->documentation!=NULL) ? sym->documentation : "";
    error(234,symbolname,ptr);  /* deprecated public function */
  } /* if */
  begcseg();
  sym->usage|=uDEFINE;  /* set the definition flag */
  if (stock)
    sym->usage|=uSTOCK;
  if (opertok!=0 && opererror)
    sym->usage &= ~uDEFINE;
  /* if the function has states, dump the label to the start of the function */
  int ovl_index=sym->index;
  if (state_id!=0) {
    assert(sym->states!=NULL);
    statelist *ptr=sym->states->next;
    while (ptr!=NULL) {
      assert(sc_status!=statWRITE || ptr->label>0);
      if (ptr->id==state_id) {
        if (pc_overlays==0)
          setlabel(ptr->label);
        ovl_index=ptr->label;
        break;          /* no need to search further */
      } /* if */
      ptr=ptr->next;
    } /* while */
  } /* if */
  startfunc(sym->name,ovl_index); /* creates stack frame */
  insert_dbgline(sym->lnumber);
  setline(false);
  if (pc_pendingflags & flgALIGNDATA)
    alignframe(pc_dataalign);
  if (pc_pendingflags & flgRECURSIVE)
    sym->flags |= flgRECURSIVE;
  pc_pendingflags=0;
  lcl_declared=0;       /* number of local cells */
  rettype=(sym->usage & uRETVALUE);      /* set "return type" variable */
  curfunc=sym;
  define_args();        /* add the symbolic info for the function arguments */
# if !defined PAWN_LIGHT
    if (matchtoken('{')) {
      lexpush();
    } else {
      /* #DOCSTRING_SEPARATOR Insert a separator so that comments following the
       * statement will not be attached to this function; they should be
       * attached to the next function. This is not a problem for functions
       * having a compound block, because the closing brace is an explicit
       * "end token" for the function. With single statement functions, the
       * preprocessor may overread the source code before the parser determines
       * an "end of statement".
       */
      insert_docstring_separator();
    } /* if */
# endif
  sc_curstates=state_id;/* set state id, for accessing global state variables */
  /* a function body without braces is a single statement at the block level of
   * the statements in a compound block (where the '{' raises it to 1)
   */
  assert(pc_blocklevel==0);
  if (!matchtoken('{'))
    pc_blocklevel=1;            /* function body is a single statement */
  else
    lexpush();
  statement(NULL,false);
  pc_blocklevel=0;
  sc_curstates=0;
  if ((rettype & uRETVALUE)!=0)
    sym->usage|=uRETVALUE;
  if (lcl_declared!=0) {
    /* This happens only in a very special (and useless) case, where a function
     * has only a single statement in its body (no compound block) and that
     * statement declares a new variable
     */
    modstk((int)lcl_declared*pc_cellsize);  /* remove all local variables */
    lcl_declared=0;
  } /* if */
  if ((lastst!=tRETURN) && (lastst!=tGOTO)){
    int remparams=(strcmp(sym->name,_ENTRYFUNC)!=0 && strcmp(sym->name,_EXITFUNC)!=0);
    ldconst(0,sPRI);
    ffret(remparams);
    if ((sym->usage & uRETVALUE)!=0 && lastst!=tENDLESS) {
      char symname[2*sNAMEMAX+16];  /* allow space for user defined operators */
      funcdisplayname(symname,countof(symname),sym->name);
      error(209,symname);       /* function should return a value */
    } /* if */
  } /* if */
  endfunc();
  /* for normal functions, set the end address of the function symbol; for
   * for functions with states, adjust the endaddr field for the particular
   * state (these fields are needed for overlays)
   */
  if (stlist==NULL)
    sym->codeaddr=code_idx;
  else
    stlist->endaddr=code_idx;
# if !defined PAWN_LIGHT
    if (sc_status==statBROWSE && stlist!=NULL && stlist->id!=FALLBACK) {
      /* run through the documentation strings, to check for transitions (but
         ignore it if this function is marked as the fallback; there is no
         purpose in documenting a fallback that causes no state switch) */
      int idx,count;
      for (idx=count=0; (str=(char*)get_docstring(idx))!=NULL && *str!=sDOCSEP; idx++)
        if (strstr(str,"<transition ")!=NULL)
          count++;
      if (count==0) {
        /* there are no transitions, so the event is handled internally;
           document this with pseudo-transitions */
        count=state_count(stlist->id);
        for (idx=0; idx<count; idx++) {
          int state_id_local=state_listitem(stlist->id,idx);
          int fsa_local=state_getfsa(stlist->id);
          size_t len;
          char *doc;
          const constvalue *state=state_findid(state_id_local,fsa_local);
          assert(state!=NULL);
          len=strlen(state->name)+50; /* +50 for the fixed part of "<transition ..." */
          if ((doc=(char*)malloc(len*sizeof(char)))!=NULL) {
            sprintf(doc,"<transition internal source=\"%s\"/>",state->name);
            insert_docstring(doc,0);  /* prefix the state information */
            free(doc);
          }
        }
      }
    }
# endif
  sc_attachdocumentation(sym,false);  /* attach collected documentation to the function */
  if (litidx>0) {               /* if there are literals defined */
    glb_declared+=litidx;
    begdseg();                  /* flip to DATA segment */
    dumplits();                 /* dump literal strings */
    litidx=0;
  } /* if */
  testsymbols(&loctab,1,true,true);     /* test for unused arguments and labels */
  delete_symbols(&loctab,0,true,true);  /* clear local variables queue */
  assert(loctab.next==NULL);
  curfunc=NULL;                 /* make sure to only clear this after all tests have run */
  if (sc_status==statSKIP) {
    sc_status=statWRITE;
    code_idx=cidx;
    glb_declared=glbdecl;
  } /* if */
  return true;
}

static bool argcompare(arginfo *a1,arginfo *a2)
{
  bool result= strcmp(a1->name,a2->name)==0;    /* name */
  if (result)
    result= a1->ident==a2->ident;               /* type/class */
# define UMASK(u) ((u) & ~(uREAD|uWRITTEN))
  if (result)
    result= UMASK(a1->usage)==UMASK(a2->usage); /* "const" and "packed" flags */
  if (result)
    result= a1->numtags==a2->numtags;           /* tags (number and names) */
  for (int i=0; result && i<a1->numtags; i++)
    result= a1->tags[i]==a2->tags[i];
  if (result)
    result= a1->numdim==a2->numdim;             /* array dimensions & index tags */
  for (int level=0; result && level<a1->numdim; level++)
    result= a1->dim[level]==a2->dim[level];
  for (int level=0; result && level<a1->numdim; level++)
    result=compare_consttable(a1->dimnames[level],a2->dimnames[level]);
  if (result)
    result= a1->hasdefault==a2->hasdefault;     /* availability of default value */
  if (a1->hasdefault) {
    if (a1->ident==iREFARRAY) {
      if (result)
        result= a1->defvalue.array.size==a2->defvalue.array.size;
      if (result)
        result= a1->defvalue.array.arraysize==a2->defvalue.array.arraysize;
      /* ??? should also check contents of the default array (these troubles
       * go away in a 2-pass compiler that forbids double declarations but
       * Pawn currently does not forbid them) */
    } else {
      if (result) {
        if ((a1->hasdefault & uSIZEOF)!=0 || (a1->hasdefault & uTAGOF)!=0)
          result= a1->hasdefault==a2->hasdefault
                  && strcmp(a1->defvalue.size.symname,a2->defvalue.size.symname)==0
                  && a1->defvalue.size.level==a2->defvalue.size.level;
        else
          result= a1->defvalue.val==a2->defvalue.val;
      } /* if */
    } /* if */
    if (result)
      result= a1->defvalue_tag==a2->defvalue_tag;
  } /* if */
  return result;
# undef UMASK
}

/** declparams() adds an entry in the local symbol table for each argument in
 *  the argument list.
 *
 *  \param sym          [in, modified] The symbol for the function.
 *  \param chkshadow    Whether the routine should check (and warn) for argument
 *                      names that shadow a global variable/constant/function.
 *
 *  \return The number of arguments.
 */
static int declparams(symbol *sym,int chkshadow)
{
# define MAXTAGS 16
  char *ptr;
  int argcnt,oldargcnt,numtags;
  cell val;
  arginfo *arglist;
  int ident,fpublic,fconst;
  int idx;

  /* if the function is already defined earlier, get the number of arguments
   * of the existing definition
   */
  oldargcnt=0;
  if ((sym->usage & uPROTOTYPED)!=0)
    while (sym->dim.arglist[oldargcnt].ident!=0)
      oldargcnt++;
  argcnt=0;                             /* zero aruments up to now */
  ident=iVARIABLE;
  numtags=0;
  fconst=false;
  fpublic= (sym->usage & uPUBLIC)!=0;
  assert(litidx==0);                    /* literal queue should be empty */
  /* the '(' parantheses has already been parsed */
  if (!matchtoken(')')){
    char name[sNAMEMAX+1];
    int tags[MAXTAGS];
    int tok;
    arginfo arg = { 0 };
    do {                                /* there are arguments; process them */
      /* any legal name increases argument count (and stack offset) */
      tok=lex(&val,&ptr);
      switch (tok) {
      case 0:
        /* nothing */
        break;
      case '&':
        if (ident!=iVARIABLE || numtags>0)
          error(1,"<identifier>","&");          /* expected -identifier- */
        else if (fconst)
          error(240);                           /* '&' is superfluous (pass by value) */
        ident=iREFERENCE;
        break;
      case tCONST:
        if (ident!=iVARIABLE || numtags>0)
          error(1,"<identifier>","const");      /* expected -identifier- */
        fconst=true;
        break;
      case tLABEL:
        if (numtags>0)
          error(1,"<identifier>","<tagname>");  /* expected -identifier- */
        tags[0]=pc_addtag(ptr);
        numtags=1;
        break;
      case '{':
        if (numtags>0)
          error(1,"<identifier>","{");          /* expected -identifier- */
        numtags=0;
        while (numtags<MAXTAGS) {
          if (!matchtoken('_') && !needtoken(tSYMBOL))
            break;
          tokeninfo(NULL,&ptr);
          tags[numtags++]=pc_addtag(ptr);
          if (matchtoken('}'))
            break;
          needtoken(',');
        } /* while */
        needtoken(':');
        tok=tLABEL;     /* for outer loop: flag that we have seen a tagname */
        break;
      case tSYMBOL:
        if (argcnt>=sMAXARGS)
          error(45);                    /* too many function parameters */
        strcpy(name,ptr);               /* save parameter name */
        if (name[0]==PUBLIC_CHAR)
          error(56,name);               /* function parameters cannot be public */
        if (numtags==0)
          tags[numtags++]=0;            /* default tag */
        /* Stack layout: (#STACKLAYOUT)
         *   base + 0*pc_cellsize  == previous "frame base"
         *   base + 1*pc_cellsize  == function return address
         *   base + 2*pc_cellsize  == number of arguments
         *   base + 3*pc_cellsize  == first argument of the function
         * So the offset of each argument is "(argcnt+3) * pc_cellsize".
         */
        param2symbol(name,ident,(argcnt+3)*pc_cellsize,tags,numtags,fpublic,fconst,chkshadow,&arg);
        if (fpublic && arg.hasdefault)
          error(59,name);       /* parameters of a public function may not have a default value */
        if ((sym->usage & uPROTOTYPED)==0) {
          /* redimension the argument list, add the entry */
          sym->dim.arglist=(arginfo*)realloc(sym->dim.arglist,(argcnt+2)*sizeof(arginfo));
          if (sym->dim.arglist==0)
            error(303);                 /* insufficient memory */
          memset(&sym->dim.arglist[argcnt+1],0,sizeof(arginfo));  /* keep the list terminated */
          /* clone the named indices (because the generated list is inherited
           * by the local symbol
           */
          for (idx=0; idx<arg.numdim; idx++) {
            if (arg.dimnames[idx]!=NULL)
              arg.dimnames[idx]=clone_consttable(arg.dimnames[idx]);
          } /* for */
          sym->dim.arglist[argcnt]=arg;
        } else {
          /* check the argument with the earlier definition */
          if (argcnt>oldargcnt || !argcompare(&sym->dim.arglist[argcnt],&arg))
            error(25);          /* function definition does not match prototype */
          /* may need to free default array argument */
          if (arg.ident==iREFARRAY && arg.hasdefault)
            free(arg.defvalue.array.data);
          else if (arg.ident==iVARIABLE
                   && ((arg.hasdefault & uSIZEOF)!=0 || (arg.hasdefault & uTAGOF)!=0))
            free(arg.defvalue.size.symname);
          /* free the tag list */
          free(arg.tags);
          /* do NOT free the named indices list, because a local symbol with this list
           * was created, and this symbol "inherited" the list
           */
        } /* if */
        argcnt++;
        ident=iVARIABLE;
        numtags=0;
        fconst=false;
        break;
      case tELLIPS:
        if (ident!=iVARIABLE)
          error(10);                    /* illegal function or declaration */
        if (numtags==0)
          tags[numtags++]=0;            /* default tag */
        if ((sym->usage & uPROTOTYPED)==0) {
          /* redimension the argument list, add the entry iVARARGS */
          sym->dim.arglist=(arginfo*)realloc(sym->dim.arglist,(argcnt+2)*sizeof(arginfo));
          if (sym->dim.arglist==0)
            error(303);                 /* insufficient memory */
          memset(&sym->dim.arglist[argcnt+1],0,sizeof(arginfo));  /* keep the list terminated */
          sym->dim.arglist[argcnt].ident=iVARARGS;
          sym->dim.arglist[argcnt].name[0]='\0';
          sym->dim.arglist[argcnt].hasdefault=false;
          sym->dim.arglist[argcnt].defvalue.val=0;
          sym->dim.arglist[argcnt].defvalue_tag=0;
          sym->dim.arglist[argcnt].numtags=numtags;
          sym->dim.arglist[argcnt].tags=(int*)malloc(numtags*sizeof tags[0]);
          if (sym->dim.arglist[argcnt].tags==NULL)
            error(303);                 /* insufficient memory */
          memcpy(sym->dim.arglist[argcnt].tags,tags,numtags*sizeof tags[0]);
        } else {
          if (argcnt>oldargcnt || sym->dim.arglist[argcnt].ident!=iVARARGS)
            error(25);          /* function definition does not match prototype */
        } /* if */
        argcnt++;
        break;
      default:
        error(10);              /* illegal function or declaration */
        litidx=0;               /* make sure literals get dropped (ignored) */
      } /* switch */
    } while (tok=='&' || tok==tLABEL || tok==tCONST
             || tok!=tELLIPS && matchtoken(',')); /* more? */
    if (tok==tELLIPS && matchtoken(',')) {
      error(99);  /* ... must appear last */
      skiptotoken(')',false);
    } else {
      needtoken(')');
    }
  } /* if */
  if ((sym->usage & uPROTOTYPED)!=0 && argcnt!=oldargcnt)
    error(25);                  /* function definition does not match prototype */

  /* resolve any "sizeof" arguments (now that all arguments are known) */
  assert(sym->dim.arglist!=NULL);
  arglist=sym->dim.arglist;
  for (idx=0; idx<argcnt && arglist[idx].ident!=0; idx++) {
    if ((arglist[idx].hasdefault & uSIZEOF)!=0 || (arglist[idx].hasdefault & uTAGOF)!=0) {
      int altidx;
      int closestdist=INT_MAX;
      int closestidx=-1;
      /* Find the argument with the name mentioned after the "sizeof" or "tagof".
       * Note that we cannot use findloc here because we need the arginfo struct,
       * not the symbol. (While searching, also keep an eye for the "closest match".)
       */
      ptr=arglist[idx].defvalue.size.symname;
      assert(ptr!=NULL);
      for (altidx=0; altidx<argcnt; altidx++) {
        int dist;
        if (strcmp(ptr,arglist[altidx].name)==0)
          break;
        dist=levenshtein_distance(arglist[altidx].name,ptr);
        if (dist<closestdist && dist<=MAX_EDIT_DIST) {
          closestidx=altidx;
          closestdist=dist;
        } /* if */
      } /* for */
      if (altidx>=argcnt) {
        /* no match found */
        if (closestidx>=0)
          error(makelong(17,1),ptr,arglist[closestidx].name);
        else
          error(17,ptr);  /* undefined symbol (meaning "undefined argument") */
      } else {
        assert(arglist[idx].defvalue.size.symname!=NULL);
        /* check the level against the number of dimensions */
        if (arglist[idx].defvalue.size.level>0
            && arglist[idx].defvalue.size.level>=arglist[altidx].numdim)
          error(28,arglist[idx].name);  /* invalid subscript */
        /* check the type of the argument whose size to take; for a iVARIABLE
         * or a iREFERENCE, this is always 1 (so the code is redundant)
         */
        assert(arglist[altidx].ident!=iVARARGS);
        if (arglist[altidx].ident!=iREFARRAY && (arglist[idx].hasdefault & uSIZEOF)!=0) {
          assert(arglist[altidx].ident==iVARIABLE || arglist[altidx].ident==iREFERENCE);
          error(223,ptr);       /* redundant sizeof */
        } /* if */
      } /* if */
    } /* if */
  } /* for */

  sym->usage|=uPROTOTYPED;
  errorset(sRESET);             /* reset error flag (clear the "panic mode")*/
  return argcnt;
}

/** param2symbol() creates a local variable for a function parameter. This
 *  routine is called from `declparams()`.
 *
 *  \param name         [in] The parameter name.
 *  \param ident        The type of variable, iVARIABLE, iREFERENCE, iREFARRAY.
 *  \param offset       The variable (base) address relative to `FRM`.
 *  \param tags         [in] An array with tag IDs (a function parameter may
 *                      accept multiple tags.
 *  \param numtags      The number of tag IDs in the `tags` array.
 *  \param fpublic      `true` if the *function* to which this parameter applies
 *                      is public. The parameters themselves are never public.
 *  \param fconst       `true` if the parameter is declared `const`.
 *  \param chkshadow    If `true`, the function checks for parameters that
 *                      shadow global variables/functions.
 *  \param arg          [out] The structure with argument information, which is
 *                      completed with collected data.
 */
static void param2symbol(const char *name,int ident,int offset,
                         const int tags[],int numtags,bool fpublic,bool fconst,
                         bool chkshadow,arginfo *arg)
{
  strlcpy(arg->name,name,countof(arg->name));
  arg->hasdefault=false;        /* preset (most common case) */
  arg->defvalue.val=0;          /* clear */
  arg->defvalue_tag=0;
  arg->numdim=0;
  arg->usage=0;
  int matchbrace=0;
  short usage=0;
  if (matchtoken('['))
    matchbrace=']';
  else if (matchtoken('{'))
    matchbrace='}';
  if (matchbrace!=0) {
    if (ident==iREFERENCE)
      error(67,name);           /* illegal declaration ("&name[]" is unsupported) */
    if (matchbrace==']') {
      do {
        if (arg->numdim==sDIMEN_MAX) {
          error(53);              /* exceeding maximum number of dimensions */
          return;
        } /* if */
        cell size=needsub(']',&arg->dimnames[arg->numdim]);/* may be zero here, it is a pointer anyway */
#       if INT_MAX < LONG_MAX
          if (size>INT_MAX)
            error(305);           /* overflow, exceeding capacity */
#       endif
        arg->dim[arg->numdim]=(int)size;
        arg->numdim+=1;
      } while (matchtoken('['));
      if (matchtoken('{'))
        matchbrace='}';
    } /* if */
    if (matchbrace=='}') {
      if (arg->numdim==sDIMEN_MAX) {
        error(53);              /* exceeding maximum number of dimensions */
        return;
      } /* if */
      usage=uPACKED;
      cell size=needsub('}',&arg->dimnames[arg->numdim]);/* may be zero here, it is a pointer anyway */
      size=((size*sCHARBITS/8)+pc_cellsize-1) / pc_cellsize;
#     if INT_MAX < LONG_MAX
        if (size>INT_MAX)
          error(305);           /* overflow, exceeding capacity */
#     endif
      arg->dim[arg->numdim]=(int)size;
      arg->numdim+=1;
    } /* if */
    if (matchtoken('['))
      error(51);                /* {} must be last */
    verify_array_namelist(arg->dimnames,arg->numdim);
    ident=iREFARRAY;            /* "reference to array" (is a pointer) */
    if (matchtoken('=')) {
      lexpush();                /* initials() needs the "=" token again */
      assert(litidx==0);        /* at the start of a function, this is reset */
      assert(numtags>0);
      cell size;
      initials(ident,usage,tags[0],&size,arg->dim,arg->numdim,arg->dimnames);
      assert(size>=0 && (unsigned)size>=litidx);
      /* allocate memory to hold the initial values (note: after an error in the
       * initializer, the literal queue is reset, meaning no data) */
      arg->defvalue.array.data=(litidx>0) ? (cell *)malloc(litidx*sizeof(cell)) : NULL;
      if (arg->defvalue.array.data!=NULL) {
        int i;
        memcpy(arg->defvalue.array.data,litq,litidx*sizeof(cell));
        arg->hasdefault=true;   /* argument has default value */
        arg->defvalue.array.size=(int)litidx;
        arg->defvalue.array.addr=-1;
        /* calulate size to reserve on the heap */
        arg->defvalue.array.arraysize=1;
        for (i=0; i<arg->numdim; i++)
          arg->defvalue.array.arraysize*=arg->dim[i];
        if (arg->defvalue.array.arraysize < arg->defvalue.array.size)
          arg->defvalue.array.arraysize = arg->defvalue.array.size;
      } /* if */
      litidx=0;                 /* reset */
    } /* if */
  } else {
    if (matchtoken('=')) {
      unsigned char size_tag_token;
      assert(ident==iVARIABLE || ident==iREFERENCE);
      arg->hasdefault=true;     /* argument has a default value */
      size_tag_token=(unsigned char)(matchtoken(tSIZEOF) ? uSIZEOF : 0);
      if (size_tag_token==0)
        size_tag_token=(unsigned char)(matchtoken(tTAGOF) ? uTAGOF : 0);
      if (size_tag_token!=0) {
        int paranthese;
        if (ident==iREFERENCE)
          error(66,name);       /* parameter may not be a reference */
        paranthese=0;
        while (matchtoken('('))
          paranthese++;
        if (needtoken(tSYMBOL)) {
          /* save the name of the argument whose size id to take */
          char *symname;
          tokeninfo(NULL,&symname);
          if ((arg->defvalue.size.symname=duplicatestring(symname)) == NULL)
            error(303);         /* insufficient memory */
          arg->defvalue.size.level=0;
          if (size_tag_token==uSIZEOF) {
            while (matchtoken('[')) {
              arg->defvalue.size.level+=(short)1;
              needtoken(']');
            } /* while */
          } /* if */
          if (ident==iVARIABLE) /* make sure we set this only if not a reference */
            arg->hasdefault |= size_tag_token;  /* uSIZEOF or uTAGOF */
        } /* if */
        while (paranthese--)
          needtoken(')');
      } else {
        constexpression(&arg->defvalue.val,&arg->defvalue_tag,NULL);
        assert(numtags>0);
        if (!matchtag(tags[0],arg->defvalue_tag,true))
          error(213);           /* tagname mismatch */
      } /* if */
    } /* if */
  } /* if */
  arg->ident=ident;
  arg->usage=(fconst ? uCONST : 0);
  arg->usage|=usage;
  arg->numtags=numtags;
  arg->tags=(int*)malloc(numtags*sizeof tags[0]);
  if (arg->tags==NULL)
    error(303);                 /* insufficient memory */
  memcpy(arg->tags,tags,numtags*sizeof tags[0]);
  symbol *argsym=findlcl(name);
  if (argsym!=NULL) {
    error(21,name);             /* symbol already defined */
  } else {
    if (chkshadow && (argsym=findglb(name,sSTATEVAR))!=NULL && argsym->ident!=iFUNCTN && !undefined_vars)
      error(219,name);          /* variable shadows another symbol (only give the warning when there are no errors) */
    /* add details of type and address */
    assert(numtags>0);
    argsym=addvariable(name,offset,ident,sLOCAL,1,tags[0],
                       arg->dim,arg->dimnames,arg->numdim,usage);
    /* add usage flags; for an array, these must also be set on the child
     * symbols of its sub-dimensions
     */
    for (symbol *sub=argsym; sub!=NULL; sub=finddepend(sub)) {
      if (ident==iREFERENCE)
        sub->usage|=uREAD;      /* because references are passed back */
      if (fpublic)
        sub->usage|=uREAD;      /* arguments of public functions are always "used" */
      if (fconst)
        sub->usage|=uCONST;
      /* arguments are at the same scope as locals in the top compound block,
       * but they are declared before that block opens, so flag this too
       */
      assert(sub->block==1);
      sub->flags|=flgARGUMENT;
    } /* for */
  } /* if */
}

static bool vararg_locals(symbol *sym)
{
  int argidx;
  for (argidx=0; sym->dim.arglist[argidx].ident!=iVARARGS && sym->dim.arglist[argidx].ident!=0; argidx++)
    /* nothing */;
  if (sym->dim.arglist[argidx].ident==0)
    return false; /* this function does not have a variable argument list */
  /* 'arg' now holds the number of fixed arguments before the '...' */

  /* create the 'argc' special variable; for the 'addr' argument of addvariable(),
   * see the comment #STACKLAYOUT in declparams()
   */
  if (findlcl("argc")==NULL) {
    symbol *argsym=addvariable("argc",2*pc_cellsize,iVARIABLE,sLOCAL,1,0,NULL,NULL,0,0);
    argsym->usage|=uREAD|uWRITTEN|uCONST|uARGC;
    argsym->index=argidx;       /* number of fixed arguments */
  } else {
    error(241,"argc");          /* function parameter shadows intrinsic variable  */
  } /* if */

  /* create the 'argv' variable; use the first tag of the variable argument list;
   * see the comment #STACKLAYOUT in declparams() for 'addr' argument of
   * addvariable()
   */
  if (findlcl("argv")==NULL) {
    int tag= (sym->dim.arglist[argidx].numtags>0) ? sym->dim.arglist[argidx].tags[0] : 0;
    int dim[sDIMEN_MAX]={0};
    constvalue *dimnames[sDIMEN_MAX]={0};
    symbol *argsym=addvariable("argv",(3+argidx)*pc_cellsize,iARRAY,sLOCAL,1,tag,dim,dimnames,1,0);
    argsym->usage|=uREAD|uWRITTEN|uARGC;
    if (sym->dim.arglist[argidx].usage & uCONST)
      argsym->usage|=uCONST;
    argsym->index=argidx;       /* number of fixed arguments */
  } else {
    error(241,"argc");          /* function parameter shadows intrinsic variable  */
  } /* if */

  return true;
}

static int count_referrers(const symbol *entry)
{
  int count=0;
  for (int i=0; i<entry->numrefers; i++)
    if (entry->refer[i]!=NULL)
      count++;
  return count;
}

#if !defined PAWN_LIGHT
static int find_xmltag(char *source,
                       const char *xmltag,const char *xmlparam,const char *xmlvalue,
                       char **outer_start,int *outer_length,
                       char **inner_start,int *inner_length)
{
  char *ptr;
  const char *inner_end;
  size_t xmltag_len,xmlparam_len,xmlvalue_len;

  assert(source!=NULL);
  assert(xmltag!=NULL);
  assert(outer_start!=NULL);
  assert(outer_length!=NULL);
  assert(inner_start!=NULL);
  assert(inner_length!=NULL);

  /* both NULL or both non-NULL */
  assert(xmlvalue!=NULL && xmlparam!=NULL || xmlvalue==NULL && xmlparam==NULL);

  xmltag_len=strlen(xmltag);
  xmlparam_len= (xmlparam!=NULL) ? strlen(xmlparam) : 0;
  xmlvalue_len= (xmlvalue!=NULL) ? strlen(xmlvalue) : 0;
  ptr=source;
  /* find an opening '<' */
  while ((ptr=strchr(ptr,'<'))!=NULL) {
    int match;
    *outer_start=ptr;           /* be optimistic... */
    match=false;                /* ...and pessimistic at the same time */
    ptr++;                      /* skip '<' */
    while (*ptr!='\0' && *ptr<=' ')
      ptr++;                    /* skip white space */
    if (strncmp(ptr,xmltag,xmltag_len)==0 && (*(ptr+xmltag_len)<=' ' || *(ptr+xmltag_len)=='>')) {
      /* xml tag found, optionally check the parameter */
      ptr+=xmltag_len;
      while (*ptr!='\0' && *ptr<=' ')
        ptr++;                  /* skip white space */
      if (xmlparam!=NULL) {
        if (strncmp(ptr,xmlparam,xmlparam_len)==0 && (*(ptr+xmlparam_len)<=' ' || *(ptr+xmlparam_len)=='=')) {
          ptr+=xmlparam_len;
          while (*ptr!='\0' && *ptr<=' ')
            ptr++;              /* skip white space */
          if (*ptr=='=') {
            ptr++;              /* skip '=' */
            while (*ptr!='\0' && *ptr<=' ')
              ptr++;            /* skip white space */
            if (*ptr=='"' || *ptr=='\'')
              ptr++;            /* skip " or ' */
            assert(xmlvalue!=NULL);
            if (strncmp(ptr,xmlvalue,xmlvalue_len)==0
                && (*(ptr+xmlvalue_len)<=' '
                    || *(ptr+xmlvalue_len)=='>'
                    || *(ptr+xmlvalue_len)=='"'
                    || *(ptr+xmlvalue_len)=='\''))
              match=true;       /* found it */
          } /* if */
        } /* if */
      } else {
        match=true;             /* don't check the parameter */
      } /* if */
    } /* if */
    if (match) {
      /* now find the end of the opening tag */
      while (*ptr!='\0' && *ptr!='>')
        ptr++;
      if (*ptr=='>')
        ptr++;
      while (*ptr!='\0' && *ptr<=' ')
        ptr++;                  /* skip white space */
      *inner_start=ptr;
      /* find the start of the closing tag (assume no nesting) */
      while ((ptr=strchr(ptr,'<'))!=NULL) {
        inner_end=ptr;
        ptr++;                  /* skip '<' */
        while (*ptr!='\0' && *ptr<=' ')
          ptr++;                /* skip white space */
        if (*ptr=='/') {
          ptr++;                /* skip / */
          while (*ptr!='\0' && *ptr<=' ')
            ptr++;              /* skip white space */
          if (strncmp(ptr,xmltag,xmltag_len)==0 && (*(ptr+xmltag_len)<=' ' || *(ptr+xmltag_len)=='>')) {
            /* find the end of the closing tag */
            while (*ptr!='\0' && *ptr!='>')
              ptr++;
            if (*ptr=='>')
              ptr++;
            /* set the lengths of the inner and outer segment */
            assert(*inner_start!=NULL);
            *inner_length=(int)(inner_end-*inner_start);
            assert(*outer_start!=NULL);
            *outer_length=(int)(ptr-*outer_start);
            break;              /* break out of the loop */
          } /* if */
        } /* if */
      } /* while */
      return true;
    } /* if */
  } /* while */
  return false; /* not found */
}

static char *xmlencode(char *dest,char *source)
{
  char temp[2*sNAMEMAX+20];
  char *ptr;

  /* replace < by &lt; and such; normally, such a symbol occurs at most once in
   * a symbol name (e.g. "operator<")
   */
  ptr=temp;
  while (*source!='\0') {
    switch (*source) {
    case '<':
      strcpy(ptr,"&lt;");
      ptr+=4;
      break;
    case '>':
      strcpy(ptr,"&gt;");
      ptr+=4;
      break;
    case '&':
      strcpy(ptr,"&amp;");
      ptr+=5;
      break;
    default:
      *ptr++=*source;
    } /* switch */
    source++;
  } /* while */
  *ptr='\0';
  strcpy(dest,temp);
  return dest;
}

static void write_docstring(FILE *log,const char *string)
{
  if (string==NULL)
    return;
  while (*string<=' ' && *string!='\0')
    string++;                 /* skip white space */
  if (*string=='\0')
    return;

  assert(strchr(string,sDOCSEP)==NULL);
  /* optionally wrap in "<summary>...</summary>", check whether this must happen */
  if (*string!='<') {       /* wrap in "summary" */
    const char *ptr;
    int len=0;
    for (ptr=string; *ptr!='\0' && *ptr!='<' && *ptr!='.'; ptr++)
      len++;
    if (*ptr=='.')
      len++;
    assert(len>0);
    fprintf(log,"\t\t\t<summary>%.*s</summary>\n",len,string);
    string+=len;
    while (*string<=' ' && *string!='\0')
      string++;             /* skip white space */
  } /* if */

  if (*string!='\0')
    fprintf(log,"\t\t\t%s\n",string);
}

static void make_report(symbol *root,FILE *log,const char *sourcefile,int *makestategraph)
{
  char symname[_MAX_PATH];
  int i,arg,dim,count;
  symbol *sym,*ref;
  const constvalue *tagsym;
  char *ptr;

  /* adapt the installation directory */
  strcpy(symname,sc_rootpath);
# if DIRSEP_CHAR=='\\'
    while ((ptr=strchr(symname,':'))!=NULL)
      *ptr='|';
    while ((ptr=strchr(symname,DIRSEP_CHAR))!=NULL)
      *ptr='/';
# endif

  /* the XML header */
  fprintf(log,"<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
  fprintf(log,"<?xml-stylesheet href=\"file:///%s/xml/pawndoc.xsl\" type=\"text/xsl\"?>\n",symname);
  fprintf(log,"<doc source=\"%s\">\n",sourcefile);
  ptr=strrchr(sourcefile,DIRSEP_CHAR);
  if (ptr!=NULL)
    ptr++;
  else
    ptr=(char*)sourcefile;
  fprintf(log,"\t<assembly>\n\t\t<name>%s</name>\n\t</assembly>\n",ptr);

  /* attach the global documentation, if any */
  if (pc_globaldoc!=NULL) {
    fprintf(log,"\n\t<!-- general -->\n");
    fprintf(log,"\t<general>\n\t\t");
    while ((ptr=strchr(pc_globaldoc,sDOCSEP))!=NULL)
      *ptr=' ';
    fputs(pc_globaldoc,log);
    fprintf(log,"\n\t</general>\n\n");
  } /* if */

  /* use multiple passes to print constants variables and functions in
   * separate sections
   */
  fprintf(log,"\t<members>\n");

  fprintf(log,"\n\t\t<!-- enumerated constants -->\n");
  for (arg=1; ; arg++) {
    count=0;
    dim=0;
    int tag=0;
    /* count how many constants there are in this list, and count how many are used */
    for (sym=root->next; sym!=NULL; sym=sym->next) {
      if (sym->ident!=iCONSTEXPR || sym->x.enumlist!=arg)
        continue;
      if (count==0)
        tag=sym->tag;
      else if (tag!=sym->tag)
        tag=0;
      count++;
      if ((sym->usage & uREAD)!=0)
        dim++;
    } /* for */
    if (count==0)
      break;            /* this list is unknown, don't look further */
    if (dim==0)
      continue;         /* none of the constants in this list is used, skip documenting it */
    /* document the list */
    if (tag!=0) {
      tagsym=find_tag_byval(tag);
      assert(tagsym!=NULL);
      fprintf(log,"\t\t<member name=\"T:%s\">\n",tagsym->name);
    } else {
      fprintf(log,"\t\t<member name=\"T:anonymous\">\n");
    } /* if */
    count=0;
    for (sym=root->next; sym!=NULL; sym=sym->next) {
      if (sym->ident!=iCONSTEXPR || sym->x.enumlist!=arg)
        continue;
      if (count==0)
        fprintf(log,"\t\t\t<location file=\"%s\" line=\"%ld\"/>\n",get_inputfile(sym->fnumber),(long)sym->lnumber);
      count++;
      fprintf(log,"\t\t\t<member name=\"C:%s\" value=\"%ld\">\n",
              funcdisplayname(symname,countof(symname),sym->name),(long)sym->addr);
      if (sym->tag!=0 && sym->tag!=tag) {
        tagsym=find_tag_byval(sym->tag);
        assert(tagsym!=NULL);
        fprintf(log,"\t\t\t\t<tagname value=\"%s\"/>\n",tagsym->name);
      } /* if */
      assert(sym->refer!=NULL);
      for (i=0; i<sym->numrefers; i++) {
        if ((ref=sym->refer[i])!=NULL)
          fprintf(log,"\t\t\t\t<referrer name=\"%s\"/>\n",
                  xmlencode(symname,funcdisplayname(symname,countof(symname),ref->name)));
      } /* for */
      write_docstring(log,sym->documentation);
      fprintf(log,"\t\t\t</member>\n");
    } /* for */
    fprintf(log,"\t\t</member>\n");
  } /* for */

  fprintf(log,"\n\t\t<!-- constants -->\n");
  for (sym=root->next; sym!=NULL; sym=sym->next) {
    if (sym->ident!=iCONSTEXPR || sym->x.enumlist!=0)
      continue;
    if ((sym->usage & uREAD)==0)
      continue;
    /* list only constants with referrers */
    count=0;
    assert(sym->refer!=NULL);
    for (i=0; i<sym->numrefers; i++)
      if ((ref=sym->refer[i])!=NULL)
        count++;
    if (count==0)
      continue;
    fprintf(log,"\t\t<member name=\"C:%s\" value=\"%ld\">\n",
            funcdisplayname(symname,countof(symname),sym->name),(long)sym->addr);
    if (sym->tag!=0) {
      tagsym=find_tag_byval(sym->tag);
      assert(tagsym!=NULL);
      fprintf(log,"\t\t\t<tagname value=\"%s\"/>\n",tagsym->name);
    } /* if */
    if (sym->fnumber>0 || sym->lnumber>0) {
      /* predefined constants do not have a "location" */
      fprintf(log,"\t\t\t<location file=\"%s\" line=\"%ld\"/>\n",get_inputfile(sym->fnumber),(long)sym->lnumber);
    } /* if */
    assert(sym->refer!=NULL);
    for (i=0; i<sym->numrefers; i++) {
      if ((ref=sym->refer[i])!=NULL)
        fprintf(log,"\t\t\t<referrer name=\"%s\"/>\n",
                xmlencode(symname,funcdisplayname(symname,countof(symname),ref->name)));
    } /* for */
    write_docstring(log,sym->documentation);
    fprintf(log,"\t\t</member>\n");
  } /* for */

  fprintf(log,"\n\t\t<!-- variables -->\n");
  for (sym=root->next; sym!=NULL; sym=sym->next) {
    if (sym->parent!=NULL)
      continue;                 /* hierarchical data type */
    if (sym->ident!=iVARIABLE && sym->ident!=iARRAY)
      continue;
    funcdisplayname(symname,countof(symname),sym->name);
    fprintf(log,"\t\t<member name=\"F:%s\" syntax=\"%s",symname,symname);
    if (sym->ident==iARRAY) {
      for (ref=sym; ref!=NULL; ref=finddepend(ref)) {
        if (ref->dim.array.names!=NULL) {
          constvalue *field=ref->dim.array.names->next;
          fprintf(log,"[");
          while (field!=NULL && strlen(field->name)>0) {
            fprintf(log,".%s",field->name);
            if (field->next!=NULL && (field->next->value - field->value) > 1)
              fprintf(log,"[%ld]",(long)(field->next->value - field->value));
            field=field->next;
            if (field!=NULL && strlen(field->name)>0)
              fprintf(log,", ");
          } /* if */
          fprintf(log,"]");
        } else {
          fprintf(log,"[%ld]",(long)ref->dim.array.length);
        } /* if */
      } /* for */
    } /* if */
    fprintf(log,"\">\n");
    if (sym->tag!=0) {
      tagsym=find_tag_byval(sym->tag);
      assert(tagsym!=NULL);
      fprintf(log,"\t\t\t<tagname value=\"%s\"/>\n",tagsym->name);
    } /* if */
    fprintf(log,"\t\t\t<location file=\"%s\" line=\"%ld\"/>\n",get_inputfile(sym->fnumber),(long)sym->lnumber);
    assert(sym->refer!=NULL);
    if ((sym->usage & uPUBLIC)!=0)
      fprintf(log,"\t\t\t<attribute name=\"public\"/>\n");
    for (i=0; i<sym->numrefers; i++) {
      if ((ref=sym->refer[i])!=NULL)
        fprintf(log,"\t\t\t<referrer name=\"%s\"/>\n",
                xmlencode(symname,funcdisplayname(symname,countof(symname),ref->name)));
    } /* for */
    write_docstring(log,sym->documentation);
    fprintf(log,"\t\t</member>\n");
  } /* for */

  fprintf(log,"\n\t\t<!-- functions -->\n");
  for (sym=root->next; sym!=NULL; sym=sym->next) {
    if (sym->parent!=NULL)
      continue;                 /* hierarchical data type */
    if (sym->ident!=iFUNCTN)
      continue;
    if ((sym->usage & (uREAD | uNATIVE))==uNATIVE)
      continue;                 /* unused native function */
    if ((sym->usage & (uPUBLIC | uDEFINE))==uPUBLIC)
      continue;
    funcdisplayname(symname,countof(symname),sym->name);
    xmlencode(symname,symname);
    fprintf(log,"\t\t<member name=\"M:%s\" syntax=\"%s(",symname,symname);
    /* print only the names of the parameters between the parentheses */
    assert(sym->dim.arglist!=NULL);
    for (arg=0; sym->dim.arglist[arg].ident!=0; arg++) {
      if (arg>0)
        fprintf(log,", ");
      switch (sym->dim.arglist[arg].ident) {
      case iVARIABLE:
        fprintf(log,"%s",sym->dim.arglist[arg].name);
        break;
      case iREFERENCE:
        fprintf(log,"&amp;%s",sym->dim.arglist[arg].name);
        break;
      case iREFARRAY:
        fprintf(log,"%s",sym->dim.arglist[arg].name);
        for (dim=0; dim<sym->dim.arglist[arg].numdim; dim++)
          fprintf(log,"[]");
        break;
      case iVARARGS:
        fprintf(log,"...");
        break;
      } /* switch */
    } /* for */
    fprintf(log,")\">\n");
    if (sym->tag!=0) {
      tagsym=find_tag_byval(sym->tag);
      assert(tagsym!=NULL);
      fprintf(log,"\t\t\t<tagname value=\"%s\"/>\n",tagsym->name);
    } /* if */
    /* check whether this function is called from the outside */
    if ((sym->usage & uNATIVE)!=0)
      fprintf(log,"\t\t\t<attribute name=\"native\"/>\n");
    if ((sym->usage & uPUBLIC)!=0)
      fprintf(log,"\t\t\t<attribute name=\"public\"/>\n");
    if ((sym->flags & flgENTRYPOINT)!=0)
      fprintf(log,"\t\t\t<attribute name=\"init\"/>\n");
    else if (strcmp(sym->name,_ENTRYFUNC)==0)
      fprintf(log,"\t\t\t<attribute name=\"entry\"/>\n");
    else if (strcmp(sym->name,_EXITFUNC)==0)
      fprintf(log,"\t\t\t<attribute name=\"exit\"/>\n");
    if ((sym->usage & uNATIVE)==0)
      fprintf(log,"\t\t\t<stacksize value=\"%ld\"/>\n",(long)sym->x.stacksize);
    //??? the report is written before the code is generated; as a result, the code size is not known at this stage
    //if ((sym->usage & uNATIVE)==0)
    //  fprintf(log,"\t\t\t<codesize value=\"%ld\"/>\n",(long)sym->codeaddr - sym->addr);
    if (sym->states!=NULL) {
      statelist *stlist=sym->states->next;
      assert(stlist!=NULL);     /* there should be at least one state item */
      if (stlist!=NULL && stlist->id==FALLBACK)
        stlist=stlist->next;    /* skip the fallback state, if that happens to be the first */
      if (stlist) {
        assert(stlist->id>0);   /* state id should be found */
        int fsa_id=state_getfsa(stlist->id);
        assert(fsa_id>=0);           /* automaton 0 exists */
        const constvalue *fsa=automaton_findid(fsa_id);
        assert(fsa!=NULL);      /* automaton should be found */
        fprintf(log,"\t\t\t<automaton name=\"%s\"/>\n", strlen(fsa->name)>0 ? fsa->name : "(anonymous)");
        if (makestategraph)
          *makestategraph=1;
      } /* if */
    } /* if */
    fprintf(log,"\t\t\t<location file=\"%s\" line=\"%ld\"/>\n",get_inputfile(sym->fnumber),(long)sym->lnumber);
    assert(sym->refer!=NULL);
    for (i=0; i<sym->numrefers; i++)
      if ((ref=sym->refer[i])!=NULL)
        fprintf(log,"\t\t\t<referrer name=\"%s\"/>\n",
                xmlencode(symname,funcdisplayname(symname,countof(symname),ref->name)));
    /* print all symbols that are required for this function to compile */
    for (ref=root->next; ref!=NULL; ref=ref->next) {
      if (ref==sym)
        continue;
      for (i=0; i<ref->numrefers; i++)
        if (ref->refer[i]==sym)
          fprintf(log,"\t\t\t<dependency name=\"%s\"/>\n",
                  xmlencode(symname,funcdisplayname(symname,countof(symname),ref->name)));
    } /* for */
    /* print parameter list, with tag & const information, plus descriptions */
    assert(sym->dim.arglist!=NULL);
    for (arg=0; sym->dim.arglist[arg].ident!=0; arg++) {
      if (sym->dim.arglist[arg].ident==iVARARGS)
        fprintf(log,"\t\t\t<param name=\"...\">\n");
      else
        fprintf(log,"\t\t\t<param name=\"%s\">\n",sym->dim.arglist[arg].name);
      /* print the tag name(s) for each parameter */
      assert(sym->dim.arglist[arg].numtags>0);
      assert(sym->dim.arglist[arg].tags!=NULL);
      int paraminfo=(sym->dim.arglist[arg].numtags>1 || sym->dim.arglist[arg].tags[0]!=0)
                    || sym->dim.arglist[arg].ident==iREFERENCE
                    || sym->dim.arglist[arg].ident==iREFARRAY;
      if (paraminfo)
        fprintf(log,"\t\t\t\t<paraminfo>");
      if (sym->dim.arglist[arg].numtags>1 || sym->dim.arglist[arg].tags[0]!=0) {
        assert(paraminfo);
        if (sym->dim.arglist[arg].numtags>1)
          fprintf(log," {");
        for (i=0; i<sym->dim.arglist[arg].numtags; i++) {
          if (i>0)
            fprintf(log,",");
          tagsym=find_tag_byval(sym->dim.arglist[arg].tags[i]);
          assert(tagsym!=NULL);
          fprintf(log,"%s",tagsym->name);
        } /* for */
        if (sym->dim.arglist[arg].numtags>1)
          fprintf(log,"}");
      } /* if */
      switch (sym->dim.arglist[arg].ident) {
      case iREFERENCE:
        fprintf(log," &amp;");
        break;
      case iREFARRAY:
        fprintf(log," ");
        for (dim=0; dim<sym->dim.arglist[arg].numdim; dim++) {
          if (sym->dim.arglist[arg].dim[dim]==0) {
            fprintf(log,"[]");
          } else if (sym->dim.arglist[arg].dimnames[dim]!=NULL) {
            constvalue *field=sym->dim.arglist[arg].dimnames[dim]->next;
            fprintf(log,"[");
            while (field!=NULL && strlen(field->name)>0) {
              fprintf(log,".%s",field->name);
              if (field->next!=NULL && (field->next->value - field->value) > 1)
                fprintf(log,"[%ld]",(long)(field->next->value - field->value));
              field=field->next;
              if (field!=NULL && strlen(field->name)>0)
                fprintf(log,", ");
            } /* if */
            fprintf(log,"]");
          } else {
            fprintf(log,"[%d]",sym->dim.arglist[arg].dim[dim]);
          } /* if */
        } /* for */
        break;
      } /* switch */
      if (paraminfo)
        fprintf(log," </paraminfo>\n");
      /* print the user description of the parameter (parse through
       * sym->documentation)
       */
      char *outer_start,*inner_start;
      int outer_length,inner_length;
      if (sym->documentation!=NULL
          && find_xmltag(sym->documentation,"param","name",sym->dim.arglist[arg].name,
                         &outer_start,&outer_length,&inner_start,&inner_length))
      {
        char *tail;
        fprintf(log,"\t\t\t\t%.*s\n",inner_length,inner_start);
        /* delete from documentation string */
        tail=outer_start+outer_length;
        memmove(outer_start,tail,strlen(tail)+1);
      } /* if */
      fprintf(log,"\t\t\t</param>\n");
    } /* for */
    write_docstring(log,sym->documentation);
    fprintf(log,"\t\t</member>\n");
  } /* for */

  fprintf(log,"\n\t</members>\n");
  fprintf(log,"</doc>\n");
}
#endif

/* Every symbol has a referrer list, that contains the functions that use
 * the symbol. Now, if function "apple" is accessed by functions "banana" and
 * "citron", but neither function "banana" nor "citron" are used by anyone
 * else, then, by inference, function "apple" is not used either.
 */
static void reduce_referrers(symbol *root)
{
  int i,restart;
  symbol *sym,*ref;

  do {
    restart=0;
    for (sym=root->next; sym!=NULL; sym=sym->next) {
      if (sym->parent!=NULL)
        continue;                 /* hierarchical data type */
      if (sym->ident==iFUNCTN
          && (sym->usage & uNATIVE)==0
          && (sym->usage & uPUBLIC)==0
          && (sym->flags & flgENTRYPOINT)==0
          && strcmp(sym->name,_ENTRYFUNC)!=0 && strcmp(sym->name,_EXITFUNC)!=0
          && count_referrers(sym)==0)
      {
        sym->usage&=~(uREAD | uWRITTEN);  /* erase usage bits if there is no referrer */
        /* find all symbols that are referred by this symbol */
        for (ref=root->next; ref!=NULL; ref=ref->next) {
          if (ref->parent!=NULL)
            continue;             /* hierarchical data type */
          assert(ref->refer!=NULL);
          for (i=0; i<ref->numrefers && ref->refer[i]!=sym; i++)
            /* nothing */;
          if (i<ref->numrefers) {
            assert(ref->refer[i]==sym);
            ref->refer[i]=NULL;
            restart++;
          } /* if */
        } /* for */
      } else if ((sym->ident==iVARIABLE || sym->ident==iARRAY)
                 && (sym->usage & uPUBLIC)==0
                 && count_referrers(sym)==0)
      {
        assert(sym->parent==NULL);  /* checked near the top of the 'for' loop */
        sym->usage&=~(uREAD | uWRITTEN);  /* erase usage bits if there is no referrer */
      } /* if */
    } /* for */
    /* after removing a symbol, check whether more can be removed */
  } while (restart>0);
}

/* Generate the overlay information; this can be done once the first passes
 * have completed, and we know which functions are actually called. The overlay
 * information is always generated; it depends on the compiler options and
 * configuration whether it is actually used.
 * This function should be called after the pre-amble is generated, because
 * the pre-amble contains "static" overlays.
 */
static void gen_ovlinfo(symbol *root)
{
  if (pc_overlays>0) {
    int idx=0;
    symbol *sym;
    int i;
    assert(pc_ovl0size[ovlEXIT][1]!=0); /* if this fails, writeleader() was not called */
    for (i=0; i<ovlFIRST; i++)
      if (pc_ovl0size[i][1]!=0)
          idx++;

    for (sym=root->next; sym!=NULL; sym=sym->next) {
      if (sym->ident==iFUNCTN && sym->parent==NULL
          && (sym->usage & uNATIVE)==0 && (sym->usage & (uREAD | uPUBLIC))!=0
          && (sym->usage & uDEFINE)!=0)
      {
        /* state entry functions are called directly for the states, but there
         * is no function stub (for the jump table) -> no overlay index should
         * be assigned for this stub
         */
        if (strcmp(sym->name,_ENTRYFUNC)!=0)
          sym->index=idx++;
        if (sym->states!=NULL) {
          /* for functions with states, allocate an index for every implementation */
          statelist *stateptr;
          for (stateptr=sym->states->next; stateptr!=NULL; stateptr=stateptr->next)
            stateptr->label=idx++;
        } /* if */
      } /* if */
    } /* for */
  } /* if */
}

#if !defined PAWN_LIGHT
static long max_stacksize_recurse(symbol **sourcesym,symbol *sym,long basesize,int *pubfuncparams,bool *recursion)
{
  long size,maxsize;
  int i,stkpos;

  assert(sourcesym!=NULL);
  assert(sym!=NULL);
  assert(sym->ident==iFUNCTN);
  assert((sym->usage & uNATIVE)==0);
  assert(recursion!=NULL);

  maxsize=sym->x.stacksize;
  for (i=0; i<sym->numrefers; i++) {
    if (sym->refer[i]!=NULL) {
      assert(sym->refer[i]->ident==iFUNCTN);
      assert((sym->refer[i]->usage & uNATIVE)==0); /* a native function cannot refer to a user-function */
      for (stkpos=0; sourcesym[stkpos]!=NULL; stkpos++) {
        if (sym->refer[i]==sourcesym[stkpos]) {   /* recursion detection */
          if ((sym->flags & flgRECURSIVE)==0) {
            char symname[2*sNAMEMAX+16];  /* allow space for user defined operators */
            funcdisplayname(symname,countof(symname),sym->name);
            error_onpos(sym->fnumber,sym->lnumber,237,symname); /* recursive function */
          } /* if */
          *recursion=true;
          goto break_recursion;         /* recursion was detected, quit loop */
        } /* if */
      } /* for */
      /* add this symbol to the stack */
      sourcesym[stkpos]=sym;
      sourcesym[stkpos+1]=NULL;
      /* check size of callee */
      size=max_stacksize_recurse(sourcesym,sym->refer[i],sym->x.stacksize,pubfuncparams,recursion);
      if (maxsize<size)
        maxsize=size;
      /* remove this symbol from the stack */
      sourcesym[stkpos]=NULL;
    } /* if */
  } /* for */
  break_recursion:

  if ((sym->usage & uPUBLIC)!=0) {
    /* Find out how many parameters a public function has, then see if this
     * is bigger than some maximum
     */
    arginfo *arg=sym->dim.arglist;
    int count=0;
    assert(arg!=0);
    while (arg->ident!=0) {
      count++;
      arg++;
    } /* while */
    assert(pubfuncparams!=0);
    if (count>*pubfuncparams)
      *pubfuncparams=count;
  } /* if */

  errorset(sEXPRRELEASE); /* clear error data */
  errorset(sRESET);

  return maxsize+basesize;
}

static long max_stacksize(symbol *root,bool *recursion)
{
  /* Loop over all non-native functions. For each function, loop
   * over all of its referrers, accumulating the stack requirements.
   * Detect (indirect) recursion with a "mark-and-sweep" algorithm.
   * I (mis-)use the "compound" field of the symbol structure for
   * the marker, as this field is unused for functions.
   *
   * Note that the stack is shared with the heap. A host application
   * may "eat" cells from the heap as well, through amx_Allot(). The
   * stack requirements are thus only an estimate.
   */
  long size,maxsize;
  int maxparams,numfunctions;
  symbol *sym;
  symbol **symstack;

  assert(root!=NULL);
  assert(recursion!=NULL);
  /* count number of functions (for allocating the stack for recursion detection) */
  numfunctions=0;
  for (sym=root->next; sym!=NULL; sym=sym->next) {
    if (sym->ident==iFUNCTN) {
      assert(sym->block==0);
      if ((sym->usage & uNATIVE)==0)
        numfunctions++;
    } /* if */
  } /* if */
  /* allocate function symbol stack */
  symstack=(symbol **)malloc((numfunctions+1)*sizeof(symbol*));
  if (symstack==NULL)
    error(303);         /* insufficient memory (fatal error) */
  memset(symstack,0,(numfunctions+1)*sizeof(symbol*));

  maxsize=0;
  maxparams=0;
  *recursion=false;     /* assume no recursion */
  for (sym=root->next; sym!=NULL; sym=sym->next) {
    /* drop out if this is not a user-implemented function */
    if (sym->ident!=iFUNCTN || (sym->usage & uNATIVE)!=0)
      continue;
    /* accumulate stack size for this symbol */
    symstack[0]=sym;
    assert(symstack[1]==NULL);
    size=max_stacksize_recurse(symstack,sym,0L,&maxparams,recursion);
    assert(size>=0);
    if (maxsize<size)
      maxsize=size;
  } /* for */

  free((void*)symstack);
  maxsize++;                  /* +1 because a zero cell is always pushed on top
                               * of the stack to catch stack overwrites */
  return maxsize+(maxparams+1);/* +1 because # of parameters is always pushed on entry */
}

static long max_overlaysize(symbol *root,char **funcname)
{
  symbol *sym;
  cell max=0;

  assert(root!=NULL);
  assert(pc_overlays>0);
  for (sym=root->next; sym!=NULL; sym=sym->next) {
    if (sym->ident!=iFUNCTN)
      continue;
    if ((sym->usage & uNATIVE)!=0)
      continue;         /* native functions have no code size */
    if ((sym->usage & (uPUBLIC | uDEFINE))==uPUBLIC)
      continue;         /* public function that is not implemented in this source code */
    if ((sym->usage & (uPUBLIC | uREAD))==0)
      continue;         /* function is not public and not used */
    if (max<(sym->codeaddr - sym->addr)) {
      max=sym->codeaddr - sym->addr;
      if (funcname!=NULL)
        *funcname=sym->name;
    } /* if */
  } /* if */
  return (long)max;
}
#endif

static bool checkundefined(symbol *root)
{
  int count=0;
  symbol *sym=root->next;
  while (sym!=NULL) {
    switch (sym->ident) {
    case iVARIABLE:
    case iREFERENCE:
    case iARRAY:
    case iREFARRAY:
      if ((sym->usage & uDEFINE)==0 && (sym->usage & (uREAD | uWRITTEN))!=0)
        count++;
      break;
    case iFUNCTN:
      if ((sym->usage & uPROTOTYPED)==0)
        count++;
      break;
    } /* if */
    sym=sym->next;
  } /* while */
  return (count>0);
}

/** testsymbols() checks for unused local or global variables.
 *
 *  \param root         [in] The root of the symbol list, which must be either
 *                      the global symbol list or the local variable list.
 *  \param level        The scope or nesting level for local symbols. It should
 *                      be 0 for the global symbol list.
 *  \param testfunclvl  Whether to test function parameters and labels (goto
 *                      targets). This parameter is only relevant for the local
 *                      symbol table. See the notes below.
 *  \param testconst    Whether to test constants are tested. The rationale is
 *                      to silently ignore global constants (no code is
 *                      generated for these anyway).
 *
 *  \return Whether there is an "entry" point for the program; it is only
 *          relevant when browsing the global symbol table.
 *
 *  \note All symbols of the given `level` and above are tested. Symbols below
 *        the given level are not tested.
 *
 *  \note "Public" functions are excluded from the check, since these may be
 *        exported to other object modules.
 *
 *  \note The rationale for testing function parameters separately at the end of
 *        a function, is that `testsymbols()` also inserts debugging info for
 *        the symbols, and the lifetime of a function parameter spans up to the
 *        epilogue. The rationale for testing goto labels at the end of a
 *        function (but not at the end of the compound block), is that a goto
 *        may jump into a label in a more nested block.
 */
static bool testsymbols(const symbol *root,short level,bool testfunclvl,bool testconst)
{
  bool entry=false;

  for (symbol *sym=root->next; sym!=NULL; sym=sym->next) {
    assert(level==0 && sym->block==0 || level>0 && sym->block>0);
    if (sym->block<level)
      continue;
    if ((sym->flags & flgARGUMENT)!=0 && !testfunclvl)
      continue;
    bool marked_unused=find_unusedvar(sym->name,level,true,NULL,NULL);
    arginfo *argptr=NULL;
    if ((sym->flags & flgARGUMENT)!=0) {
      /* find the argument matching the symbol */
      assert(sym->block==1);  /* arguments have block level 1 */
      assert(curfunc!=NULL);  /* must be inside a function to have block>0 */
      arginfo *arglist=curfunc->dim.arglist;
      assert(arglist!=NULL);
      for (int arg=0; arglist[arg].ident!=0 && argptr==NULL; arg++) {
        if (strcmp(sym->name,arglist[arg].name)==0) {
          assert(arglist[arg].ident==sym->ident);
          argptr=&arglist[arg];
        } /* if */
      } /* for */
    } /* if */
    switch (sym->ident) {
    case iLABEL:
      if (testfunclvl) {
        if ((sym->usage & uDEFINE)==0)
          error_suggest(19,sym->name,iLABEL);  /* not a label: ... */
        else if ((sym->usage & uREAD)==0 && !marked_unused)
          error_onpos(sym->fnumber,sym->lnumber,203,sym->name); /* symbol isn't used: ... */
      } /* if */
      break;
    case iFUNCTN:
      if ((sym->usage & (uDEFINE | uREAD | uNATIVE | uSTOCK | uPUBLIC))==uDEFINE && !marked_unused) {
        char symname[2*sNAMEMAX+16];
        funcdisplayname(symname,countof(symname),sym->name);
        if (strlen(symname)>0)
          error_onpos(sym->fnumber,sym->lnumber,203,symname);   /* symbol isn't used ... (and not public/native/stock) */
      } /* if */
      if (((sym->usage & uPUBLIC)!=0 || (sym->flags & flgENTRYPOINT)!=0) && (sym->usage & uDEFINE)!=0)
        entry=true;             /* there is an entry point */
      /* also mark the function to the debug information */
      if (((sym->usage & uREAD)!=0 || (sym->usage & uPUBLIC)!=0 && (sym->usage & uDEFINE)!=0)
          && (sym->usage & uNATIVE)==0)
        insert_dbgsymbol(sym);
      break;
    case iCONSTEXPR:
      if (testconst && (sym->usage & uREAD)==0 && !marked_unused)
        error_onpos(sym->fnumber,sym->lnumber,203,sym->name);   /* symbol isn't used: ... */
      break;
    default:
      /* a variable */
      if (sym->parent!=NULL)
        break;                  /* ignore sub-symbols of a hierarchical data type */
      if (!marked_unused) {
        if ((sym->usage & (uWRITTEN|uREAD|uSTOCK))==0)
          error_onpos(sym->fnumber,sym->lnumber,203,sym->name,sym->lnumber);  /* symbol isn't used (and not stock) */
        else if ((sym->usage & (uREAD|uSTOCK|uPUBLIC))==0 && (sym->ident==iVARIABLE || sym->ident==iARRAY))
          error_onpos(sym->fnumber,sym->lnumber,204,sym->name);   /* value assigned to symbol is never used */
      } /* if */
      if (argptr!=NULL && sym->ident==iREFARRAY && (sym->usage & (uWRITTEN|uPUBLIC|uCONST))==0) {
        /* only function arguments can have iREFARRAY type, when the symbol is
         * neither const nor written to, check for a default value (for a
         * non-const array argument, the default parameter must be copied to
         * the heap; when it is const, its address in the DATA section can be
         * passed directly)
         */
        assert(curfunc!=NULL);  /* must be inside a function to have scope>0 */
        assert((sym->flags & flgARGUMENT)!=0 && sym->block==1);
        if (argptr->hasdefault!=0)
          error_onpos(sym->fnumber,sym->lnumber,214,sym->name);   /* make array argument "const" */
      } /* if */
      /* also mark the variable (local or global) to the debug information */
      if ((sym->usage & (uWRITTEN|uREAD))!=0)
        insert_dbgsymbol(sym);
    } /* if */
    /* copy uREAD and uWRITTEN flags from the symbol to the argument info */
    if (argptr!=NULL)
      argptr->usage|=(sym->usage & (uREAD|uWRITTEN));
  } /* for */

  /* also check the list of symbol names for which a "#pragma unused" was set,
   * but for which a declaration is missing
   * but... arguments and labels at the lowest block level (level 1) are tested
   * at the end of the function, and so the "#pragma unused" entries of that
   * level are resolved then, not when the top block closes.
	 */
  if (level!=1 || testfunclvl) {
    const char *name;
    short filenr,scope;
    int linenr;
    bool handled;
    for (int idx=0; (name=get_unusedvar(idx,&filenr,&linenr,&scope,&handled)); idx++) {
      if (!handled && scope>=level)
        error_onpos(filenr,linenr,17,name);   /* undefined symbol */
    } /* for */
    delete_unusedvartable(level);
  } /* if */

  errorset(sEXPRRELEASE); /* clear error data */
  errorset(sRESET);
  return entry;
}

/* test_skippedundef() counts how many "undefined" symbols in fact... exist.
 * If in a preprocessor expression (like #if) there is a test whether a symbol
 * exists and that symbol is not defined, it is added to a list. If that symbol
 * turns out to exist after all at the end of a parse, then:
 * a) that symbol was "late defined"
 * b) the next parse will choose a different through the compiled sections
 *    (because the symbol is now defined)
 * That "next parse" should be another iteration of a "browse" parse, because
 * the single code generation parse must run through the same code as what the
 * browse phase has seen.
 */
static int test_skippedundef(void)
{
  int count=0;
  short filenr;
  int linenr;
  const char *symname;

  for (int idx=0; (symname=get_undefsymbol(idx,&filenr,&linenr))!=NULL; idx++) {
    /* test only functions; the local symbol table has already been erased
       and constants & variables (and the text substution macros) must be
       defined before use) */
    const symbol *sym=findglb(symname,sGLOBAL);
    if (sym!=NULL && sym->ident==iFUNCTN && (sym->usage & uPROTOTYPED)!=0) {
      /* normally, errors & warnings only show during "write" status, but this
         test only occurs during "browse" phase (and the warning will go away
         in write phase, because an extra pass has then been made) -> temporarily
         switch to "write" state just for the warning */
      assert(sc_status==statBROWSE);
      sc_status=statWRITE;
      error_onpos(filenr,linenr,208,symname);
      sc_status=statBROWSE;
      count++;  /* symbol that was undefined when tested, became defined */
    }
  }

  errorset(sEXPRRELEASE); /* clear error data */
  errorset(sRESET);
  return count;
}

static cell calc_array_datasize(symbol *sym, cell *offset)
{
  cell length;

  assert(sym!=NULL);
  assert(sym->ident==iARRAY || sym->ident==iREFARRAY);
  length=sym->dim.array.length;
  if (sym->dim.array.level > 0) {
    cell sublength=calc_array_datasize(finddepend(sym),offset);
    if (offset!=NULL)
      *offset=length*(*offset+pc_cellsize);
    if (sublength>0)
      length*=length*sublength;
    else
      length=0;
  } else {
    if (offset!=NULL)
      *offset=0;
  } /* if */
  return length;
}

static void destructsymbols(symbol *root,int level)
{
  cell offset=0;
  int savepri=false;
  symbol *sym=root->next;
  while (sym!=NULL && sym->block>=level) {
    if (sym->ident==iVARIABLE || sym->ident==iARRAY) {
      /* check that the '~' operator is defined for this tag */
      char symbolname[sNAMEMAX+1];
      operator_symname(symbolname,countof(symbolname),"~",sym->tag,0,1,0);
      symbol *opsym;
      if ((opsym=findglb(symbolname,sGLOBAL))!=NULL) {
        cell elements;
        /* save PRI, in case of a return statment */
        if (!savepri) {
          pushreg(sPRI);        /* right-hand operand is in PRI */
          savepri=true;
        } /* if */
        /* if the variable is an array, get the number of elements */
        if (sym->ident==iARRAY) {
          elements=calc_array_datasize(sym,&offset);
          /* "elements" can be zero when the variable is declared like
           *    new mytag: myvar[2][] = { {1, 2}, {3, 4} }
           * one should declare all dimensions!
           */
          if (elements==0)
            error(46,sym->name);        /* array size is unknown */
        } else {
          elements=1;
          offset=0;
        } /* if */
        pushval(elements);
        /* call the '~' operator */
        address(sym,sPRI);
        addconst(offset);       /* add offset to array data to the address */
        pushreg(sPRI);
        pushval(2*pc_cellsize); /* 2 parameters */
        assert(opsym->ident==iFUNCTN);
        ffcall(opsym,NULL,1);
        if (sc_status!=statSKIP)
          markusage(opsym,uREAD);   /* do not mark as "used" when this call itself is skipped */
        if ((opsym->usage & uNATIVE)!=0 && opsym->x.lib!=NULL)
          opsym->x.lib->value += 1; /* increment "usage count" of the library */
      } /* if */
    } /* if */
    sym=sym->next;
  } /* while */
  /* restore PRI, if it was saved */
  if (savepri)
    popreg(sPRI);
}

static constvalue *insert_constval(constvalue *prev,constvalue *next,const char *name,cell val,
                                   int index)
{
  constvalue *cur=(constvalue*)malloc(sizeof(constvalue));
  if (cur==NULL)
    error(303);       /* insufficient memory (fatal error) */
  memset(cur,0,sizeof(constvalue));
  if (name!=NULL) {
    assert(strlen(name)<=sNAMEMAX);
    strcpy(cur->name,name);
  } /* if */
  cur->value=val;
  cur->index=index;
  cur->usage=0;
  cur->next=next;
  prev->next=cur;
  return cur;
}

SC_FUNC constvalue *append_constval(constvalue *table,const char *name,cell val,int index)
{
  constvalue *cur,*prev;

  /* find the end of the constant table */
  for (prev=table, cur=table->next; cur!=NULL; prev=cur, cur=cur->next)
    /* nothing */;
  return insert_constval(prev,NULL,name,val,index);
}

SC_FUNC constvalue *find_constval(constvalue *table,const char *name,int index)
{
  constvalue *ptr = table->next;

  while (ptr!=NULL) {
    if (strcmp(name,ptr->name)==0 && (index<0 || ptr->index==index))
      return ptr;
    ptr=ptr->next;
  } /* while */
  return NULL;
}

static constvalue *find_constval_byval(constvalue *table,cell val)
{
  constvalue *ptr = table->next;

  while (ptr!=NULL) {
    if (ptr->value==val)
      return ptr;
    ptr=ptr->next;
  } /* while */
  return NULL;
}

#if 0   /* never used */
static int delete_constval(constvalue *table,char *name)
{
  constvalue *prev = table;
  constvalue *cur = prev->next;

  while (cur!=NULL) {
    if (strcmp(name,cur->name)==0) {
      prev->next=cur->next;
      free(cur);
      return true;
    } /* if */
    prev=cur;
    cur=cur->next;
  } /* while */
  return false;
}
#endif

static constvalue *clone_consttable(const constvalue *table)
{
  constvalue *cur,*root;

  if (table==NULL)
    return NULL;

  /* create new root */
  if ((root=(constvalue*)malloc(sizeof(constvalue)))==NULL)
    error(303);       /* insufficient memory (fatal error) */
  memset(root,0,sizeof(constvalue));

  /* walk through the source, copy fields */
  cur=table->next;
  while (cur!=NULL) {
    append_constval(root,cur->name,cur->value,cur->index);
    cur=cur->next;
  } /* while */
  return root;
}

SC_FUNC void delete_consttable(constvalue *table)
{
  constvalue *cur=table->next, *next;

  while (cur!=NULL) {
    next=cur->next;
    free(cur);
    cur=next;
  } /* while */
  memset(table,0,sizeof(constvalue));
}

SC_FUNC bool compare_consttable(const constvalue *table1,const constvalue *table2)
{
  if (table1==NULL && table2==NULL)
    return true;
  else if (table1==NULL || table2==NULL)
    return false;

  assert(table1!=NULL && table2!=NULL);
  constvalue *cur1=table1->next;
  constvalue *cur2=table2->next;
  while (cur1!=NULL && cur2!=NULL) {
    if (cur1->index!=cur2->index || cur1->value!=cur2->value)
      return false;
    if (strcmp(cur1->name,cur2->name)!=0)
      return false;
    cur1=cur1->next;
    cur2=cur2->next;
  } /* while */

  return true;
}

/*  add_constant
 *
 *  Adds a symbol to the symbol table. Returns NULL on failure.
 */
SC_FUNC symbol *add_constant(const char *name,cell val,int scope,int tag)
{
  symbol *sym;

  /* Test whether a global or local symbol with the same name exists. Since
   * constants are stored in the symbols table, this also finds previously
   * defind constants. */
  sym=findglb(name,sSTATEVAR);
  if (!sym)
    sym=findlcl(name);
  if (sym) {
    if (sym->ident!=iCONSTEXPR || sym->tag!=tag) {
      /* redefinition a function/variable to a constant is not allowed;
       * redefinition of a constant to a different tag is not allowed
       */
      error(21,name);           /* symbol already defined */
      return NULL;
    } else if (sym->addr!=val) {
      error(201,name);          /* redefinition of constant (different value) */
      sym->addr=val;            /* set new value */
    } /* if */
    /* silently ignore redefinitions of constants with the same value & tag */
    return sym;
  } /* if */

  /* constant doesn't exist yet */
  sym=addsym(name,val,iCONSTEXPR,scope,(scope==sGLOBAL) ? 0 : pc_blocklevel,tag,uDEFINE);
  assert(sym!=NULL);            /* fatal error 103 must be given on error */
  if (sc_status == statIDLE)
    sym->usage |= uPREDEF;
  return sym;
}

/** statement() parses a single statement. Compound statements are handled
 *  and seen as a "single" statement; local variable declarations are also
 *  handled as a special case.
 *
 *  \param lastindent   [in/out, optional] The current indentation level,
 *                      which may be adjusted in a compound statement.
 *                      This parameter may be `NULL`.
 *  \param allow_decl   Whether to allow local variable declarations.
 *
 *  \note For statements inside a compound block, `lastindent` should be
 *        non-`NULL` and `allow_decl` should be `true`. For single-line
 *        statements, `lastindent` should be `NULL` and `allow_decl` should be
 *        `false`.
 */
/* statement() is entered once for every level of nesting of statements; the
 * depth is limited, because the recursion would otherwise overrun the stack of
 * the compiler
 */
static void statement(int *lastindent,bool allow_decl)
{
  if (++pc_stmtnesting>sNESTING_MAX)
    error(302,"statement nesting");     /* table overflow (fatal error) */
  statement_nested(lastindent,allow_decl);
  pc_stmtnesting--;
}

static void statement_nested(int *lastindent,bool allow_decl)
{
  if (!freading) {
    error(36);                  /* empty statement */
    return;
  } /* if */
  errorset(sRESET);

  cell val;
  char *st;
  int tok=lex(&val,&st);
  if (tok!='{') {
    insert_dbgline(pc_curline);
    setline(true);
  } /* if */
  /* lex() has set pc_stmtindent */
  if (lastindent!=NULL && tok!=tLABEL) {
    if (*lastindent>=0 && *lastindent!=pc_stmtindent && !indent_nowarn) {
      if (pc_matchedtabsize>1 || !lex_adjusttabsize(*lastindent))
        error(217);             /* loose indentation */
    } /* if */
    *lastindent=pc_stmtindent;
    indent_nowarn=false;        /* if warning was blocked, re-enable it */
  } /* if */
  switch (tok) {
  case 0:
    /* nothing */
    break;
  case tNEW:
  case tVAR:
    if (allow_decl) {
      declloc(false);
      lastst=tVAR;
    } else {
      error(3);                 /* declaration only valid in a block */
    } /* if */
    break;
  case tSTATIC:
    if (allow_decl) {
      declloc(true);
      lastst=tVAR;
    } else {
      error(3);                 /* declaration only valid in a block */
    } /* if */
    break;
  case '{': {
    int orgline=pc_curline;
    short orglevel=pc_blocklevel;
    const unsigned char *body=lexptr; /* the text behind the '{', before matchtoken() reads on */
    pc_blocklevel+=1;           /* increase compound statement level */
    if (!matchtoken('}'))       /* {} is the empty statement */
      compound((orgline==pc_curline && freading) ? body : NULL);
    /* lastst (for "last statement") does not change */
    pc_blocklevel=orglevel;     /* restore compound statement level */
    break;
  } /* case */
  case ';':
    error(36);                  /* empty statement */
    break;
  case tIF:
    lastst=doif();
    break;
  case tWHILE:
    lastst=dowhile();
    break;
  case tDO:
    lastst=dodo();
    break;
  case tFOR:
    lastst=dofor();
    break;
  case tSWITCH:
    doswitch();
    lastst=tSWITCH;
    break;
  case tCASE:
  case tDEFAULT:
    error(14);     /* not in switch */
    break;
  case tGOTO:
    dogoto();
    lastst=tGOTO;
    break;
  case tLABEL:
    dolabel();
    lastst=tLABEL;
    break;
  case tRETURN:
    doreturn();
    lastst=tRETURN;
    break;
  case tBREAK:
    dobreak();
    lastst=tBREAK;
    break;
  case tCONTINUE:
    docont();
    lastst=tCONTINUE;
    break;
  case tEXIT:
    doexit();
    lastst=tEXIT;
    break;
  case tASSERT:
    doassert();
    lastst=tASSERT;
    break;
  case tSLEEP:
    dosleep();
    lastst=tSLEEP;
    break;
  case tSTATE:
    dostate();
    lastst=tSTATE;
    break;
  case tCONST:
    decl_const(sLOCAL);
    break;
  default:          /* non-empty expression */
    sc_allowproccall=true;
    lexpush();      /* analyze token later */
    doexpr(true,true,true,true,NULL,NULL,false,0);
    if (matchtoken('}'))
      lexpush();    /* a closing brace terminates the expression, but must be re-analysed */
    else
      needtoken(tTERM);
    lastst=tEXPR;
    sc_allowproccall=false;
  } /* switch */
}

/** compound() handles a compound statement.
 *
 *  \param stmt_sameline  If valid (i.e. not `NULL`), it was determined that at
 *                        least one token (other than '}') appears on the same
 *                        line as the '{' that started the compound statement.
 *                        This parameter then points in the current source line
 *                        and behind the '{'. This pointer must be `NULL`
 *                        otherwise.
 *
 *  \note The '{' that starts a compound statement is already matched.
 *
 *  \note Global references: srcline (read-only)
 */
static void compound(const unsigned char *stmt_sameline)
{
  int indent=-1;
  cell save_decl=lcl_declared;
  int count_stmt=0;
  int block_start=pc_curline; /* save line where the compound block started */

  /* if there is more text on this line, we should adjust the statement indent;
     when 'stmt_sameline' is non-NULL, 'srcline' holds at least two tokens (one
     of which is '{'), and 'stmt_sameline' points behind that '{' */
  if (stmt_sameline!=NULL) {
    assert(stmt_sameline!=NULL && stmt_sameline>srcline && *(stmt_sameline-1)=='{');
    const unsigned char *p=stmt_sameline;
    /* go forward, skipping white-space */
    while (*p<=' ' && *p!='\0')
      p++;
    assert(*p!='\0');         /* a token must be found, because 'stmt_sameline' is
                                 only valid when there is something behind the '{' */
    lex_fetchindent(srcline,p);
  } /* if */

  while (!matchtoken('}')){     /* repeat until compound statement is closed */
    if (!freading){
      error(30,block_start);    /* compound block not closed at end of file */
      break;
    } else {
      if (count_stmt>0 && (lastst==tRETURN || lastst==tBREAK || lastst==tCONTINUE || lastst==tENDLESS))
        error(225);             /* unreachable code */
      statement(&indent,true);  /* do a statement */
      count_stmt++;
    } /* if */
  } /* while */
  if (lastst!=tRETURN)
    destructsymbols(&loctab,pc_blocklevel);
  if (lastst!=tRETURN && lastst!=tGOTO)
    modstk((int)(lcl_declared-save_decl)*pc_cellsize);  /* delete local variable space */
  testsymbols(&loctab,pc_blocklevel,false,true);        /* look for unused block locals */
  lcl_declared=save_decl;
  delete_symbols(&loctab,pc_blocklevel,false,true);     /* erase local symbols, except
                                                         * the labels (in the function) */
}

/*  doexpr
 *
 *  Global references: stgidx   (read-only)
 */
static int doexpr(int comma,int chkeffect,int allowarray,int mark_endexpr,
                  int *tag,symbol **symptr,bool chkfuncresult,int in_test)
{
  unsigned index;
  int ident;
  int localstaging=false;
  cell val;

  if (!staging) {
    stgset(true);               /* start stage-buffering */
    localstaging=true;
    assert(stgidx==0);
  } /* if */
  index=stgidx;
  errorset(sEXPRMARK);
  do {
    /* on second round through, mark the end of the previous expression */
    if (index!=stgidx)
      markexpr(sEXPR,NULL,0);
    pc_sideeffect=false;
    ident=expression(&val,tag,symptr,NULL,chkfuncresult,in_test);
    if (!allowarray && (ident==iARRAY || ident==iREFARRAY))
      error(33,"-unknown-");    /* array must be indexed */
    if (chkeffect && !pc_sideeffect) {
      /* if the expression follows an "else", maybe the user forgot an "if" */
      if (lastst==tELSE)
        error(makelong(215,3),"if");  /* did you mean "else if (expr)?" */
      else
        error(215);             /* expression has no effect */
    }
    sc_allowproccall=false;     /* cannot use "procedure call" syntax anymore */
  } while (comma && matchtoken(',')); /* more? */
  if (mark_endexpr)
    markexpr(sEXPR,NULL,0);     /* optionally, mark the end of the expression */
  errorset(sEXPRRELEASE);
  if (localstaging) {
    stgout(index);
    stgset(false);              /* stop staging */
  } /* if */
  return ident;
}

/** constexpression() parses an expression and verify that it is a constant.
 *
 *  \param val          [out] The value of the constant expression (0 or error).
 *  \param tag          [out] The tag of the constant expresstion (0 or error).
 *  \param symptr       [out] A pointer to the symbol in the case that the
 *                      expression is a symbolic constant (set to `NULL`
 *                      otherwise).
 *
 *  \return `true` on success, `false` on error or if the expression is not
 *          constant.
 */
SC_FUNC bool constexpression(cell *val,int *tag,symbol **symptr)
{
  assert(!staging);
  stgset(true);         /* start stage-buffering */
  unsigned index;
  cell cidx;
  stgget(&index,&cidx); /* mark position in code generator */
  errorset(sEXPRMARK);
  int ident=expression(val,tag,symptr,NULL,false,0);
  stgdel(index,cidx);   /* scratch generated code */
  stgset(false);        /* stop stage-buffering */
  if (ident!=iCONSTEXPR) {
    error(8);           /* must be constant expression */
    litidx=0;           /* clear the literal pool */
    if (val!=NULL)
      *val=0;
    if (tag!=NULL)
      *tag=0;
    if (symptr!=NULL)
      *symptr=NULL;
  } /* if */
  errorset(sEXPRRELEASE);
  return (ident==iCONSTEXPR);
}

/** test() parses a test expression (and generates code for it).
 *
 *  \param label          Label number to branch to on a false branch.
 *  \param invert         Whether the test must be inverted (which is the case
 *                        for the "assert" statement).
 *  \param test_true      Whether to allows a simple constant 'true' as the
 *                        constant. This is for the special case of `while true`
 *                        which is accepted without warning.
 *  \param parenthesized  [out, optional] This parameter is set to `true` if the
 *                        test expression is enclosed with parentheses. This
 *                        parameter may be set to `NULL`.
 *
 *  \return
 *
 *  \note In the case a "simple assignment" operator ("=") is used within a
 *        test, the warning "possibly unintended assignment" is flagged. In the
 *        case the assignment was intended, use parantheses around the
 *        expression and compare to zero (to avoid the warning).
 */
static int test(int label,bool invert,bool test_true,bool *parenthesized)
{
  unsigned index;
  cell cidx;
  int ident,tag,tok;
  cell constval;
  symbol *sym;

  bool localstaging=false;
  if (!staging) {
    stgset(true);               /* start staging */
    localstaging=true;
#   if !defined NDEBUG
      stgget(&index,&cidx);     /* should start at zero if started locally */
      assert(index==0);
#   endif
  } /* if */

  bool parens=true;
  int simple_true= test_true ? NOT_SET : false;
  do {
    bool subparens;
    stgget(&index,&cidx);       /* mark position (of last expression) in
                                 * code generator */
    ident=expression(&constval,&tag,&sym,&subparens,true,sTEST_EXPR);
    parens=parens && subparens;
    if (simple_true==NOT_SET)
      simple_true= (ident==iCONSTEXPR && sym!=NULL && strcmp(sym->name,"true")==0);
    tok=matchtoken(',');
    if (tok) {
      markexpr(sEXPR,NULL,0);
      sc_allowproccall=false;   /* cannot use "procedure call" syntax anymore */
      parens=false;             /* expression is not fully parenthesized if comma was found */
      simple_true=false;
    }
  } while (tok); /* do */
  if (parenthesized!=NULL)
    *parenthesized=parens;
  if (ident==iARRAY || ident==iREFARRAY) {
    char *ptr=(sym!=NULL && strlen(sym->name)>0) ? sym->name : "-unknown-";
    error(33,ptr);              /* array must be indexed */
  } /* if */
  if (ident==iCONSTEXPR) {      /* constant expression */
    int testtype=0;
    stgdel(index,cidx);
    if (constval) {             /* code always executed */
      if (simple_true!=true)
        error(206);             /* redundant test: always non-zero */
      testtype=tENDLESS;
    } else {
      error(205);               /* redundant code: never executed */
      if (!invert)
        jumplabel(label);
    } /* if */
    if (localstaging) {
      stgout(0);                /* write "jumplabel" code */
      stgset(false);            /* stop staging */
    } /* if */
    return testtype;
  } /* if */
  if (tag!=0 && tag!=pc_addtag("bool"))
    if (check_userop(lneg,tag,0,1,NULL,&tag))
      invert= !invert;          /* user-defined ! operator inverted result */
  if (invert)
    jmp_ne0(label);             /* jump to label if true (different from 0) */
  else
    jmp_eq0(label);             /* jump to label if false (equal to 0) */
  markexpr(sEXPR,NULL,0);       /* end expression (give optimizer a chance) */
  if (localstaging) {
    stgout(0);                  /* output queue from the very beginning (see
                                 * assert() when localstaging is set to true) */
    stgset(false);              /* stop staging */
  } /* if */
  return 0;
}

static int doif(void)
{
  int ifindent=pc_stmtindent;   /* save the indent of the "if" instruction */
  int flab1=getlabel();         /* get label number for false branch */
  bool parens;
  test(flab1,false,false,&parens); /* get expression, branch to flab1 if false */
  if (!parens && lexpeek()!='{')
    needtoken(tTERM);           /* if expression is not parenthesized and not followed by '{', must end with newline */
  statement(NULL,false);        /* if true, do a statement */
  if (!matchtoken(tELSE)) {     /* if...else ? */
    setlabel(flab1);            /* no, simple if..., print false label */
  } else {
    int lastst_true=lastst;     /* save last statement of the "true" branch */
    int flab2=getlabel();
    /* to avoid the "dangling else" error, we want a warning if the "else"
     * has a lower indent than the matching "if" */
    if (pc_stmtindent<ifindent)
      error(217);               /* loose indentation */
    if ((lastst!=tRETURN) && (lastst!=tGOTO))
      jumplabel(flab2);         /* "true" branch jumps around "else" clause, unless the "true" branch statement already jumped */
    lastst=tELSE;               /* flag this as an "else" clause, for testing expressions */
    setlabel(flab1);            /* print false label */
    statement(NULL,false);      /* do "else" clause */
    setlabel(flab2);            /* print true label */
    /* if both the "true" branch and the "false" branch ended with the same
     * kind of statement, set the last statement id to that kind, rather than
     * to the generic tIF; this allows for better "unreachable code" checking
     */
    if (lastst==lastst_true)
      return lastst;
  } /* if */
  return tIF;
}

static int dowhile(void)
{
  int wq[wqSIZE];               /* allocate local queue */
  int save_endlessloop,retcode;

  save_endlessloop=endlessloop;
  addwhile(wq);                 /* add entry to queue for "break" */
  setlabel(wq[wqLOOP]);         /* loop label */
  /* The debugger uses the "break" opcode to be able to "break" out of
   * a loop. To make sure that each loop has a break opcode, even for the
   * tiniest loop, set it below the top of the loop
   */
  setline(true);
  bool parens;
  endlessloop=test(wq[wqEXIT],false,true,&parens);/* branch to wq[wqEXIT] if false */
  if (!parens && lexpeek()!='{')
    needtoken(tTERM);           /* if expression is not parenthesized and not followed by '{', must end with newline */
  statement(NULL,false);        /* if so, do a statement */
  jumplabel(wq[wqLOOP]);        /* and loop to "while" start */
  setlabel(wq[wqEXIT]);         /* exit label */
  delwhile();                   /* delete queue entry */

  retcode=endlessloop ? tENDLESS : tWHILE;
  endlessloop=save_endlessloop;
  return retcode;
}

/*
 *  Note that "continue" will in this case not jump to the top of the loop, but
 *  to the end: just before the true-or-false testing code.
 */
static int dodo(void)
{
  int wq[wqSIZE];
  int save_endlessloop;
  int retcode,top;

  save_endlessloop=endlessloop;
  addwhile(wq);           /* see "dowhile" for more info */
  top=getlabel();         /* make a label first */
  setlabel(top);          /* loop label */
  statement(NULL,false);
  needtoken(tWHILE);
  setlabel(wq[wqLOOP]);   /* "continue" always jumps to WQLOOP. */
  setline(true);
  int parens=matchtoken('(');
  endlessloop=test(wq[wqEXIT],false,false,NULL);
  jumplabel(top);
  setlabel(wq[wqEXIT]);
  delwhile();
  if (parens==1)
    needtoken(')');
  needtoken(tTERM);

  retcode=endlessloop ? tENDLESS : tDO;
  endlessloop=save_endlessloop;
  return retcode;
}

static int dofor(void)
{
  int wq[wqSIZE];
  cell save_decl;
  int save_nestlevel,save_endlessloop,skiplab;
  int result,parens;
  int *ptr;

  save_decl=lcl_declared;
  save_nestlevel=pc_blocklevel;
  save_endlessloop=endlessloop;

  addwhile(wq);
  skiplab=getlabel();
  parens=matchtoken('(');
  if (!matchtoken(';')) {
    /* new variable declarations are allowed here */
    if (matchtoken(tNEW) || matchtoken(tVAR)) {
      /* The variable in expr1 of the for loop is at a
       * 'compound statement' level of it own.
       */
      pc_blocklevel++;
      declloc(false);           /* declare local variable */
    } else {
      doexpr(true,true,true,true,NULL,NULL,false,0);  /* expression 1 */
      needtoken(';');
    } /* if */
  } /* if */
  /* Adjust the "declared" field in the "while queue", in case that local
   * variables were declared in the first expression of the "for" loop.
   * These are deleted separately, so a "break" or a "continue" must ignore
   * these fields.
   */
  ptr=readwhile();
  assert(ptr!=NULL);
  ptr[wqBRK]=(int)lcl_declared;
  ptr[wqCONT]=(int)lcl_declared;
  jumplabel(skiplab);           /* skip expression 3 1st time */
  setlabel(wq[wqLOOP]);         /* "continue" goes to this label: expr3 */
  setline(true);
  /* Expressions 2 and 3 are reversed in the generated code: expression 3
   * precedes expression 2. When parsing, the code is buffered and marks for
   * the start of each expression are insterted in the buffer.
   */
  assert(!staging);
  stgset(true);                 /* start staging */
  assert(stgidx==0);
  stgmark(sSTARTREORDER);
  stgmark((unsigned char)(sEXPRSTART+0));    /* mark start of 2nd expression in stage */
  setlabel(skiplab);                         /* jump to this point after 1st expression */
  if (matchtoken(';')) {
    endlessloop=1;
  } else {
    endlessloop=test(wq[wqEXIT],false,false,NULL);/* expression 2 (jump to wq[wqEXIT] if false) */
    needtoken(';');
  } /* if */
  stgmark((unsigned char)(sEXPRSTART+1));    /* mark start of 3th expression in stage */
  if (parens) {
    if (!matchtoken(')')) {
      doexpr(true,true,true,true,NULL,NULL,false,sFOR_POST);  /* expression 3 */
      needtoken(')');
    } /* if */
  } else {
    if (matchtoken('{')==1) {
      lexpush();                /* analyzed again in statement() */
    } else {
      doexpr(true,true,true,true,NULL,NULL,false,sFOR_POST);  /* expression 3 */
      if (lexpeek()!='{')
        needtoken(tTERM);       /* if expression is not parenthesized and not followed by '{', must end with newline */
    } /* if */
  } /* if */
  stgmark(sENDREORDER);         /* mark end of reversed evaluation */
  stgout(0);
  stgset(false);                /* stop staging */
  statement(NULL,false);
  jumplabel(wq[wqLOOP]);
  setlabel(wq[wqEXIT]);
  delwhile();

  assert(pc_blocklevel>=save_nestlevel);
  if (pc_blocklevel>save_nestlevel) {
    /* Clean up the space and the symbol table for the local
     * variable in "expr1".
     */
    destructsymbols(&loctab,pc_blocklevel);
    modstk((int)(lcl_declared-save_decl)*pc_cellsize);
    testsymbols(&loctab,pc_blocklevel,false,true);  /* look for unused block locals */
    lcl_declared=save_decl;
    delete_symbols(&loctab,pc_blocklevel,false,true);
    pc_blocklevel=save_nestlevel;     /* reset 'compound statement' nesting level */
  } /* if */

  result=endlessloop ? tENDLESS : tFOR;
  endlessloop=save_endlessloop;
  return result;
}

/* The switch statement is incompatible with its C sibling:
 * 1. the cases are not drop through
 * 2. only one instruction may appear below each case, use a compound
 *    instruction to execute multiple instructions
 * 3. the "case" keyword accepts a comma separated list of values to
 *    match, it also accepts a range using the syntax "1 .. 4"
 *
 * SWITCH param
 *   PRI = expression result
 *   param = table offset (code segment)
 *
 */
static void doswitch(void)
{
  int lbl_table,lbl_exit,lbl_case;
  int tok;
  cell val;
  char *str;
  constvalue caselist = {NULL,"",0,0,0};   /* case list starts empty */
  constvalue *cse,*csp;
  int label;

  int parens=matchtoken('(');
  doexpr(true,false,false,false,NULL,NULL,true,sTEST_EXPR);/* evaluate switch expression */
  if (parens)
    needtoken(')');
  /* generate the code for the switch statement, the label is the address
   * of the case table (to be generated later).
   */
  lbl_table=getlabel();
  lbl_case=0;                   /* just to avoid a compiler warning */
  ffswitch(lbl_table,false);

  needtoken('{');
  lbl_exit=getlabel();          /* get label number for jumping out of switch */
  bool swdefault=false;
  int casecount=0;
  do {
    tok=lex(&val,&str);         /* read in (new) token */
    switch (tok) {
    case tCASE: {
      if (swdefault)
        error(15);        /* "default" case must be last in switch statement */
      lbl_case=getlabel();
      bool save_allowtags=sc_allowtags;
      sc_allowtags=false; /* do not allow tagnames here */
      do {
        casecount++;
        constexpression(&val,NULL,NULL);
        /* Search the insertion point (the table is kept in sorted order, so
         * that advanced abstract machines can sift the case table with a
         * binary search). Check for duplicate case values at the same time.
         */
        for (csp=&caselist, cse=caselist.next;
             cse!=NULL && cse->value<val;
             csp=cse, cse=cse->next)
          /* nothing */;
        if (cse!=NULL && cse->value==val)
          error(40,(long)val);          /* duplicate "case" label */
        /* Since the label is stored as a string in the "constvalue", the
         * size of an identifier must be at least 1/4th of a cell size in bits
         * (there are 8 hexadecimal digits in a 32-bit number).
         */
#       if sNAMEMAX < PAWN_CELL_SIZE / 4
#         error Length of identifier (sNAMEMAX) too small.
#       endif
        assert(csp!=NULL);
        assert(csp->next==cse);
        insert_constval(csp,cse,celltohex(lbl_case),val,0);
        if (matchtoken(tDBLDOT)) {
          cell end;
          constexpression(&end,NULL,NULL);
          if (end<=val)
            error(50);                  /* invalid range */
          while (++val<=end) {
            casecount++;
            /* find the new insertion point */
            for (csp=&caselist, cse=caselist.next;
                 cse!=NULL && cse->value<val;
                 csp=cse, cse=cse->next)
              /* nothing */;
            if (cse!=NULL && cse->value==val)
              error(40,(long)val);      /* duplicate "case" label */
            assert(csp!=NULL);
            assert(csp->next==cse);
            insert_constval(csp,cse,celltohex(lbl_case),val,0);
          } /* if */
        } /* if */
      } while (matchtoken(','));
      needtoken(':');                   /* ':' ends the case */
      sc_allowtags=save_allowtags;      /* reset */
      setlabel(lbl_case);
      statement(NULL,false);
      jumplabel(lbl_exit);
      break;
    } /* case */
    case tDEFAULT:
      if (swdefault)
        error(16);         /* multiple defaults in switch */
      lbl_case=getlabel();
      setlabel(lbl_case);
      needtoken(':');
      swdefault=true;
      statement(NULL,false);
      /* Jump to lbl_exit, even thouh this is the last clause in the
       * switch, because the jump table is generated between the last
       * clause of the switch and the exit label.
       */
      jumplabel(lbl_exit);
      break;
    default:
      if (tok!='}') {
        error(2);
        indent_nowarn=true; /* disable this check */
        tok='}';     /* break out of the loop after an error */
      } /* if */
    } /* switch */
  } while (tok!='}');

# if !defined NDEBUG
    /* verify that the case table is sorted (unfortunatly, duplicates can
     * occur; there really shouldn't be duplicate cases, but the compiler
     * may not crash or drop into an assertion for a user error). */
    for (cse=caselist.next; cse!=NULL && cse->next!=NULL; cse=cse->next)
      assert(cse->value <= cse->next->value);
# endif
  /* generate the table here, before lbl_exit (general jump target) */
  setlabel(lbl_table);
  assert(swdefault==false || swdefault==true);
  if (swdefault==false) {
    /* store lbl_exit as the "none-matched" label in the switch table */
    label=lbl_exit;
  } else {
    /* lbl_case holds the label of the "default" clause */
    label=lbl_case;
  } /* if */
  ffcase(casecount,label,true,false);
  /* generate the rest of the table */
  for (cse=caselist.next; cse!=NULL; cse=cse->next)
    ffcase(cse->value,strtol(cse->name,NULL,16),false,false);

  setlabel(lbl_exit);
  delete_consttable(&caselist); /* clear list of case labels */
}

static void doassert(void)
{
  if ((sc_debug & sCHKBOUNDS)!=0) {
    int flab1=getlabel();       /* get label number for "OK" branch */
    int pass=test(flab1,true,false,NULL);/* get expression and branch to flab1 if true */
    insert_dbgline(pc_curline); /* make sure we can find the correct line number */
    if (pass==0)
      ffabort(xASSERTION);      /* don't halt on constant non-zero expression */
    setlabel(flab1);
  } else {
    unsigned index;
    cell cidx;
    stgset(true);               /* start staging */
    stgget(&index,&cidx);       /* mark position in code generator */
    do {
      expression(NULL,NULL,NULL,NULL,false,sTEST_EXPR);
      stgdel(index,cidx);       /* just scrap the code */
    } while (matchtoken(','));
    stgset(false);              /* stop staging */
  } /* if */
  needtoken(tTERM);
}

static void dogoto(void)
{
  /* if we were inside an endless loop, assume that we jump out of it */
  endlessloop=0;

  char *lexstr;
  cell lexval;
  if (lex(&lexval,&lexstr)==tSYMBOL) {
    symbol *sym=fetchlab(lexstr);
    jumplabel((int)sym->addr);
    /* for a backward goto, destruct the symbols at the deeper nested levels;
     * for a forward goto, the block nesting level of the label is yet unknown
     * and therefore the decision to destruct symbols cannot be made; the
     * trouble is: by the time we get to the label, the symbol may no longer be
     * in the symbol table
     */
    if ((sym->usage & uDEFINE)!=0) {
      if (sym->block<pc_blocklevel)
        destructsymbols(&loctab,sym->block+1);
    } else {
      if ((sym->usage & uREAD)==0)
        sym->x.declared=(int)lcl_declared;  /* save how many locals are declared (on first goto) */
    } /* if */
    sym->usage|=uREAD;          /* set "uREAD" bit (label is jumped to) */
  } else {
    error_suggest(20,lexstr,iLABEL);/* illegal symbol name */
  } /* if */
  needtoken(tTERM);
}

static void dolabel(void)
{
  char *name;
  tokeninfo(NULL,&name);  /* retrieve label name again */
  if (find_constval(&tagname_tab,name,-1)!=NULL)
    error(221,name);      /* label name shadows tagname */
  symbol *sym=fetchlab(name);
  assert(sym!=NULL);
  if ((sym->usage & uDEFINE)!=0)
    error(21,name);       /* symbol already defined */
  setlabel((int)sym->addr);
  /* since one can jump around variable declarations or out of compound
   * blocks, the stack must be manually adjusted
   */
  setstk(-lcl_declared*pc_cellsize);
  /* if the label was jumped to before it was defined, and local variables are
   * declared in-between the goto and the label, those variables are not
   * initialized (even though the stack was adjusted to make space for these
   * variables)
   */
  if ((sym->usage & (uDEFINE|uREAD))==uREAD && sym->x.declared<(int)lcl_declared)
    error(239);         /* 'goto' jumps around local variable declarations */
  sym->usage|=uDEFINE;  /* label is now defined */
}

/*  fetchlab
 *
 *  Finds a label from the (local) symbol table or adds one to it.
 *  Labels are local in scope.
 *
 *  Note: The "_usage" bit is set to zero. The routines that call "fetchlab()"
 *        must set this bit accordingly.
 */
static symbol *fetchlab(const char *name)
{
  symbol *sym;

  sym=findlcl(name);            /* labels are local in scope */
  if (sym) {
    if (sym->ident!=iLABEL)
      error_suggest(19,sym->name,iLABEL);  /* not a label: ... */
  } else {
    sym=addsym(name,getlabel(),iLABEL,sLOCAL,pc_blocklevel,0,0);
    assert(sym!=NULL);          /* fatal error 103 must be given on error */
    sym->block=pc_blocklevel;
  } /* if */
  return sym;
}

/*  doreturn
 *
 *  Global references: rettype  (modified)
 */
static void doreturn(void)
{
  int tag;
  int remparams;
  symbol *sym,*sub;

  assert(curfunc!=NULL);
  if (!matchtoken(tTERM)) {
    /* "return <value>" */
    int ident;
    if ((rettype & uRETNONE)!=0)
      error(78);                        /* mix "return;" and "return value;" */
    ident=doexpr(true,false,true,false,&tag,&sym,true,0);
    needtoken(tTERM);
    markexpr(sEXPR,NULL,0);
    if (ident==iARRAY && sym==NULL) {
      /* returning a literal string is not supported (it must be a variable) */
      error(96,curfunc->name);
      ident=iCONSTEXPR;                 /* avoid handling an "array" case */
    } /* if */
    /* see if this function already has a sub type (an array attached) */
    sub=finddepend(curfunc);
    assert(sub==NULL || sub->ident==iREFARRAY);
    if ((rettype & uRETVALUE)!=0) {
      int retarray=(ident==iARRAY || ident==iREFARRAY);
      /* there was an earlier "return" statement in this function */
      if (sub==NULL && retarray || sub!=NULL && !retarray)
        error(79);                      /* mixing "return array;" and "return value;" */
      if (retarray && (curfunc->usage & uPUBLIC)!=0)
        error(90,curfunc->name);        /* public function may not return array */
    } /* if */
    rettype|=uRETVALUE;                 /* function returns a value */
    /* check tagname with function tagname */
    assert(curfunc!=NULL);
    if (!matchtag(curfunc->tag,tag,true))
      error(213);                       /* tagname mismatch */
    if (ident==iARRAY || ident==iREFARRAY) {
      int dim[sDIMEN_MAX];
      constvalue *dimnames[sDIMEN_MAX];
      int numdim=0;
      cell arraysize;
      int level;
      assert(sym!=NULL);
      if (sub!=NULL) {
        assert(sub->ident==iREFARRAY);
        /* this function has an array attached already; check that the current
         * "return" statement returns exactly the same array
         */
        level=sym->dim.array.level;
        if (sub->dim.array.level!=level) {
          error(48);                    /* array dimensions must match */
        } else {
          for (numdim=0; numdim<=level; numdim++) {
            dim[numdim]=(int)sub->dim.array.length;
            dimnames[numdim]=sub->dim.array.names;
            if (sym->dim.array.length!=dim[numdim])
              error(47);    /* array sizes must match */
            if (!compare_consttable(sym->dim.array.names,dimnames[numdim]))
              error(47);    /* array definitions must match */
            if (numdim<level) {
              sym=finddepend(sym);
              sub=finddepend(sub);
              assert(sym!=NULL && sub!=NULL);
              /* ^^^ both arrays have the same dimensions (this was checked
               *     earlier) so the dependend should always be found
               */
            } /* if */
          } /* for */
        } /* if */
      } else {
        int argcount,usage=0;
        /* this function does not yet have an array attached; clone the
         * returned symbol beneath the current function
         */
        sub=sym;
        assert(sub!=NULL);
        level=sub->dim.array.level;
        for (numdim=0; numdim<=level; numdim++) {
          dim[numdim]=(int)sub->dim.array.length;
          dimnames[numdim]=clone_consttable(sub->dim.array.names);
          usage=(sub->usage & uPACKED);
          if (numdim<level) {
            sub=finddepend(sub);
            assert(sub!=NULL);
          } /* if */
          /* check that all dimensions are known */
          if (dim[numdim]<=0)
            error(46,sym->name);
        } /* for */
        /* the address of the array is stored in a hidden parameter; the address
         * of this parameter is 1 + the number of parameters (times the size of
         * a cell) + the size of the stack frame and the return address
         *   base + 0*pc_cellsize         == previous "base"
         *   base + 1*pc_cellsize         == function return address
         *   base + 2*pc_cellsize         == number of arguments
         *   base + 3*pc_cellsize         == first argument of the function
         *   ...
         *   base + ((n-1)+3)*pc_cellsize == last argument of the function
         *   base + (n+3)*pc_cellsize     == hidden parameter with array address
         */
        assert(curfunc!=NULL);
        assert(curfunc->dim.arglist!=NULL);
        for (argcount=0; curfunc->dim.arglist[argcount].ident!=0; argcount++)
          /* nothing */;
        sub=addvariable(curfunc->name,(argcount+3)*pc_cellsize,iREFARRAY,
                        sGLOBAL,0,curfunc->tag,dim,dimnames,numdim,usage);
        sub->parent=curfunc;
        /* Function that returns array can be used before it is defined, so at
         * the call point (if it is before definition) we may not know if this
         * function returns array and what is its size (for example inside the
         * conditional operator), so we don't know how many cells on the heap
         * we need. Calculating heap consumption is required for the fix of
         * incorrect heap deallocation on conditional operator. That's why we
         * need an additional pass.
         */
        if ((curfunc->usage & uREAD)!=0)
          sc_reparse=true;              /* no warning, because a forward declare won't fix this */
      } /* if */
      /* get the hidden parameter, copy the array (the array is on the heap;
       * it stays on the heap for the moment, and it is removed -usually- at
       * the end of the expression/statement, see expression() in SC3.C)
       */
      address(sub,sALT);                /* ALT = destination */
      arraysize=calc_arraysize(dim,numdim,0);
      memcopy(arraysize*pc_cellsize);   /* source already in PRI */
      /* moveto1(); is not necessary, callfunction() does a popreg() */
    } /* if */
  } else {
    /* this return statement contains no expression */
    ldconst(0,sPRI);
    if ((rettype & uRETVALUE)!=0) {
      char symname[2*sNAMEMAX+16];      /* allow space for user defined operators */
      assert(curfunc!=NULL);
      funcdisplayname(symname,countof(symname),curfunc->name);
      error(209,symname);               /* function should return a value */
    } /* if */
    rettype|=uRETNONE;                  /* function does not return anything */
  } /* if */
  destructsymbols(&loctab,0);           /* call destructor for *all* locals */
  modstk((int)lcl_declared*pc_cellsize);/* end of function, remove *all* local variables */
  assert(curfunc!=NULL);
  remparams=(strcmp(curfunc->name,_ENTRYFUNC)!=0 && strcmp(curfunc->name,_EXITFUNC)!=0);
  ffret(remparams);
}

static void dobreak(void)
{
  const int *ptr;

  endlessloop=0;      /* if we were inside an endless loop, we just jumped out */
  ptr=readwhile();      /* readwhile() gives an error if not in loop */
  needtoken(tTERM);
  if (ptr==NULL)
    return;
  destructsymbols(&loctab,pc_blocklevel);
  modstk(((int)lcl_declared-ptr[wqBRK])*pc_cellsize);
  jumplabel(ptr[wqEXIT]);
}

static void docont(void)
{
  const int *ptr;

  ptr=readwhile();      /* readwhile() gives an error if not in loop */
  needtoken(tTERM);
  if (ptr==NULL)
    return;
  destructsymbols(&loctab,pc_blocklevel);
  modstk(((int)lcl_declared-ptr[wqCONT])*pc_cellsize);
  jumplabel(ptr[wqLOOP]);
}

SC_FUNC void exporttag(int tag)
{
  /* find the tag by value in the table, then set the top bit to mark it
   * "public"
   */
  if (tag!=0 && (tag & PUBLICTAG)==0) {
    constvalue *ptr;
    for (ptr=tagname_tab.next; ptr!=NULL && tag!=(int)(ptr->value & TAGMASK); ptr=ptr->next)
      /* nothing */;
    if (ptr!=NULL)
      ptr->value |= PUBLICTAG;
  } /* if */
}

static void doexit(void)
{
  int tag=0;

  if (matchtoken(tTERM)==0){
    doexpr(true,false,false,false,&tag,NULL,true,0);
    needtoken(tTERM);
  } else {
    ldconst(0,sPRI);
  } /* if */
  ldconst(tag,sALT);
  exporttag(tag);
  destructsymbols(&loctab,0);           /* call destructor for *all* locals */
  ffabort(xEXIT);
}

static void dosleep(void)
{
  int tag=0;

  if (matchtoken(tTERM)==0){
    doexpr(true,false,false,false,&tag,NULL,true,0);
    needtoken(tTERM);
  } else {
    ldconst(0,sPRI);
  } /* if */
  ldconst(tag,sALT);
  exporttag(tag);
  ffabort(xSLEEP);

  /* for stack usage checking, mark the use of the sleep instruction */
  pc_memflags |= suSLEEP_INSTR;
}

static void dostate(void)
{
  char fsaname[sNAMEMAX+1],statename[sNAMEMAX+1];
  constvalue *automaton;
  constvalue *state;
  statelist *stlist;
  int label_failcond;
  int result;
  symbol *sym;
  constvalue dummyautomaton,dummystate;

  /* check for an optional condition */
  if (matchtoken('(')) {
    label_failcond=getlabel();  /* get label number for "false" branch */
    pc_docexpr=true;            /* attach expression as a documentation string */
    test(label_failcond,false,false,NULL);  /* get expression, branch to flabel if false */
    pc_docexpr=false;
    needtoken(')');
  } else {
    label_failcond=-1;
  } /* if */

  result=sc_getstateid(&automaton,&state,fsaname,statename);
  needtoken(tTERM);
  if (!result) {
    /* the state is unknown, it may be used before definition;
     * since the report is built after the first compile pass, create a dummy
     * state, so that the transitions can be documented
     */
    if (sc_status==statBROWSE) {
      memset(&dummyautomaton,0,sizeof dummyautomaton);
      assert(automaton==NULL || strcmp(automaton->name,fsaname)==0);
      assert(strlen(fsaname)<=sNAMEMAX);
      strcpy(dummyautomaton.name,fsaname);
      dummyautomaton.index= (automaton!=NULL) ? automaton->index : 1;
      automaton=&dummyautomaton;
      memset(&dummystate,0,sizeof dummystate);
      assert(strlen(statename)<=sNAMEMAX);
      strcpy(dummystate.name,statename);
      state=&dummystate;
    } else {
      delete_autolisttable();
      return;
    } /* if */
  } /* if */

  /* see if the current automaton has exit functions for the states, if so,
   * call it through the function stub
   */
  if ((sym=findglb(_EXITFUNC,sSTATEVAR))!=NULL) {
    /* push 0 for number of arguments in case of overlays (the state exit
     * function has a RET for normal mode, but an IRETN for overlay mode;
     * this IRETN clears the arguments from the stack)
     */
    if (pc_overlays>0)
      pushval(0);
    ffcall(sym,NULL,0);
  } /* if */

  /* check whether variables need to be cleared */
  bool alt_loaded=false;
  for (sym=glbtab.next; sym!=NULL; sym=sym->next) {
    assert(sym->states==NULL || sym->states->next!=NULL);
    if (sym->ident==iVARIABLE
        && sym->parent==NULL
        && sym->states!=NULL
        && state_getfsa(sym->states->next->id)==automaton->index
        && state_inlist(sym->states->next->id,state->value)
        && (sym->fvisible==NOT_SET || sym->fvisible==fcurrent))
    {
      /* state variable that belongs to the same automaton and whose state-list
       * contains the target state
       */
      /* step 1: build a list of all states in the automaton in the variable's
       *  state list and mark them as "present" or "absent"; also keep the
       *  counts of both
       */
      int idx;
      int count_present=0,count_absent=0;
      int top=state_top_id(automaton->index);
      unsigned char *slist=malloc((top+1));
      if (slist==NULL)
        error(303);                     /* insufficient memory */
      memset(slist,0xff,(top+1));
      for (idx=1; idx<=top; idx++) {
        if (state_findid(idx,automaton->index)!=NULL) {
          slist[idx]=state_inlist(sym->states->next->id,idx);
          if (slist[idx])
            count_present++;
          else
            count_absent++;
        } /* if */
      } /* for */
      /* step 2: generate code to check the current state (the state being
       * exited) and compare it to the "present"/"absent" flags in the list;
       * select to loop over "present" or "absent" flags depending on which
       * count is the lowest (to minimize the generated code)
       */
      if (count_absent>0 || count_present>0) {
        assert(!staging);
        stgset(true);                   /* start staging */
        assert(stgidx==0);
        if (!alt_loaded) {
          assert(automaton!=NULL);
          loadreg(automaton->value,sALT); /* read current state */
          alt_loaded=true;
        } /* if */
        if (count_absent<count_present) {
          /* this variable has to be cleared (zero'ed out) for *any* of the
           * states in the list marked as "absent"
           */
          for (idx=1; idx<=top; idx++) {
            if (slist[idx]==0) {
              int label=getlabel();
              ob_eq_val(idx);           /* is ALT (current state) equal to this state in the list? */
              jmp_eq0(label);           /* if not, skip zero'ing code */
              storeconst(sym->addr,0);  /* if so, clear */
              setlabel(label);
            }
          } /* for */
        } else {
          /* this variable has to be cleared (zero'ed out) for all states,
           *  *except* those marked as "present"
           */
          assert(count_present<=count_absent);
          int label=getlabel();
          for (idx=1; idx<=top; idx++) {
            if (slist[idx]==1) {
              ob_eq_val(idx);           /* is ALT (current state) equal to this state in the list? */
              jmp_ne0(label);           /* if so, skip zero'ing code */
            }
          } /* for */
          storeconst(sym->addr,0);      /* if dropped through all tests, clear variable */
          setlabel(label);
        } /* if */
        stgout(0);                      /* optimize and write */
        stgset(false);                  /* stop staging */
      } /* if */
      free(slist);
    } /* if */
  } /* for */

  /* store the new state id */
  assert(!staging);
  stgset(true);                   /* start staging */
  assert(stgidx==0);
  assert(state!=NULL);
  ldconst(state->value,sPRI);
  assert(automaton!=NULL);
  assert(automaton->index==0 && automaton->name[0]=='\0' || automaton->index>0);
  storereg(automaton->value,sPRI);
  stgout(0);                      /* optimize and write */
  stgset(false);                  /* stop staging */

  /* find the optional entry() function for the state */
  sym=findglb(_ENTRYFUNC,sGLOBAL);
  if (sc_status==statWRITE && sym!=NULL && sym->ident==iFUNCTN && sym->states!=NULL) {
    for (stlist=sym->states->next; stlist!=NULL; stlist=stlist->next) {
      assert(stlist->label>0);
      if (state_getfsa(stlist->id)==automaton->index && state_inlist(stlist->id,(int)state->value))
        break;      /* found! */
    } /* for */
    assert(stlist==NULL || state_inlist(stlist->id,(int)state->value));
    if (stlist!=NULL) {
      /* when generating overlays, make sure to push in a zero for the number
       * of arguments (when not using overlays, the compiler generates a RET
       * for the entry function return, but for overlays, there only exists an
       * IRETN instruction, which clears the arguments from the stack)
       */
      if (pc_overlays>0)
        pushval(0);
      /* the label to jump to is in stlist->name */
      ffcall(sym,celltohex(stlist->label),0);
    } /* if */
  } /* if */

  if (label_failcond>=0)
    setlabel(label_failcond);           /* condition was false, jump around the state switch */

# if !defined PAWN_LIGHT
    /* mark for documentation */
    assert(state!=NULL && state->name!=NULL);
    if (sc_status==statBROWSE && strlen(state->name)>0) {
      const char *str;
      int listid;
      /* get the last list-id attached to the function, this contains the source
         states for the active function */
      assert(curfunc!=NULL);
      if (curfunc->states!=NULL) {
        stlist=curfunc->states->next;
        assert(stlist!=NULL);
        while (stlist->next!=NULL)
          stlist=stlist->next;
        listid=stlist->id;
      } else {
        listid=-1;
      } /* if */
      /* get the automaton id. for the current function, to check whether to
         include the automaton name */
      int curautomaton=(listid>0) ? state_getfsa(listid) : -1;
      assert(state!=NULL && state->name!=NULL && strlen(state->name)>0);
      assert(automaton!=NULL && automaton->name!=NULL);
      size_t length=strlen(statename)+strlen(automaton->name)+70; /* +70 for the fixed part "<transition ... />\n" */
      /* see if there are any condition strings to attach */
      for (int index=0; (str=get_autolist(index))!=NULL; index++)
        length+=strlen(str);
      char *doc=(char*)malloc(length*sizeof(char));
      if (doc!=NULL) {
        int listindex=0;
        do {
          /* this may be a transition for the current automaton, or a transition
             for a different one */
          if (state->index==curautomaton)
            sprintf(doc,"<transition target=\"%s\"",statename);
          else
            sprintf(doc,"<transition target=\"%s:%s\"",automaton->name,statename);
          if (listid>=0) {
            /* get the source state */
            int stateindex=state_listitem(listid,listindex);
            state=state_findid(stateindex,curautomaton);
            assert(state!=NULL);
            sprintf(doc+strlen(doc)," source=\"%s\"",state->name);
          } /* if */
          if (get_autolist(0)!=NULL) {
            /* add the condition */
            strcat(doc," condition=\"");
            for (int index=0; (str=get_autolist(index))!=NULL; index++) {
              /* remove the ')' token that may be appended before detecting that the expression has ended */
              if (*str!=')' || *(str+1)!='\0' || get_autolist(index+1)!=NULL)
                strcat(doc,str);
            } /* for */
            strcat(doc,"\"");
          } /* if */
          strcat(doc,"/>");
          insert_docstring(doc,0);  /* prefix the state information */
        } while (listid>=0 && ++listindex<state_count(listid));
        free(doc);
      } /* if */
    } /* if */
# endif
  delete_autolisttable();
}

static void addwhile(int *ptr)
{
  int k;

  ptr[wqBRK]=(int)lcl_declared;     /* stack pointer (for "break") */
  ptr[wqCONT]=(int)lcl_declared;    /* for "continue", possibly adjusted later */
  ptr[wqLOOP]=getlabel();
  ptr[wqEXIT]=getlabel();
  if (wq_ptr>=(wq_glb+wqTABSZ-wqSIZE))
    error(302,"loop table");    /* loop table overflow (too many active loops)*/
  k=0;
  while (k<wqSIZE){     /* copy "ptr" to while queue table */
    *wq_ptr=*ptr;
    wq_ptr+=1;
    ptr+=1;
    k+=1;
  } /* while */
}

static void delwhile(void)
{
  if (wq_ptr>wq_glb)
    wq_ptr-=wqSIZE;
}

static int *readwhile(void)
{
  if (wq_ptr<=wq_glb){
    error(24);          /* out of context */
    return NULL;
  } else {
    return (wq_ptr-wqSIZE);
  } /* if */
}

