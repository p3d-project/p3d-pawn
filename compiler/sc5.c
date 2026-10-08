/*  Pawn compiler - Error message output
 *
 *  Copyright (c) 1997-2026, CompuPhase and contributors
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
 *  Version: $Id: sc5.c 7708 2026-10-07 18:58:58Z thiadmer $
 */
#include <assert.h>
#if defined __WIN32__ || defined _WIN32 || defined __MSDOS__
# include <io.h>
#endif
#if defined __LINUX__ || defined __GNUC__
# include <unistd.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>     /* ANSI standardized variable argument list functions */
#include <string.h>
#if defined FORTIFY
# include <alloc/Fortify.h>
#endif

#if defined __WIN32__ || defined _WIN32 || defined WIN32 || defined __NT__
# define DLLEXPORT __declspec (dllexport)
#elif defined HAVE_VISIBILITY
# define DLLEXPORT __attribute__ ((visibility("default")))
#endif
#include "sc.h"

#if defined _MSC_VER
# pragma warning(push)
# pragma warning(disable:4125)  /* decimal digit terminates octal escape sequence */
#endif

#include "sc5.scp"

#if defined _MSC_VER
# pragma warning(pop)
#endif

#define FATAL_BASE      100     /**< first 'fatal error' number */
#define WARN_BASE       200     /**< first warning number */
#define WARN_COUNT      (sizeof warnmsg / sizeof warnmsg[0])
typedef struct s_warnstack {
  struct s_warnstack *next;
  unsigned char flag[(WARN_COUNT+7)/8];	/* 8 flags in a char */
} warnstack;
/* the root entry holds the active flags, any other allocated entries contain
 * "pushed" flags
 */
static warnstack warndisable;
#define WARN_IDX(n)         (((n)-WARN_BASE)/8)
#define WARN_MASK(n)        (unsigned char)(1 << (((n)-WARN_BASE)%8))
#define WARN_ISDISABLED(n)  ( ((n)>=WARN_BASE && (n)<WARN_BASE+WARN_COUNT) \
                              ? (warndisable.flag[WARN_IDX(n)] & WARN_MASK(n)) != 0 \
                              : false )

static bool errflag=false;
static int errfile;     /**< forced file number for the error message */
static int errline;     /**< forced line number for the error message */
static int errstart;    /**< line number at which the instruction started */
static int warnignore;  /**< line number for warning to ignore (when error was also given for the line) */
static int errcount;    /**< number of errors */
static int warncount;   /**< number of warnings */
static bool errhidden;  /**< are there more errors than have been displayed */
static int errnotice;   /**< explicitly set notice number */

/** error_v() outputs an error message (internal 'work horse' function).
 *
 *  \param number       The error/warning number. The upper half of the value
 *                      may contain a 'notice' number to show extra information.
 *  \param argptr       A pointer to parameters of the error/warning message.
 *
 *  \return This function always returns 0.
 *
 *  \note If an error is found, the variable "errflag" is set and subsequent
 *        errors are ignored until lex() finds a semicolon or a keyword (lex()
 *        resets "errflag" in that case).
 *
 *  \note On a fatal error, the function aborts the program, with a `longjmp` to
 *        the clean-up code.
 *
 *  \note Global references: inpfname   (read-only)
 *                           pc_curline (read-only)
 *                           fcurrent   (read-only)
 *                           sc_status  (read-only)
 *                           errflag    (modified)
 *                           errline    (modified)
 *                           errfile    (modified)]
 */
static int error_v(unsigned long number,va_list argptr)
{
static const char *prefix[3]={ "error", "fatal error", "warning" };
static int lastline,errcount_on_line;
static short lastfile;

  /* split the error field between the real error/warning number and an optional
   * "notice" number
   */
  unsigned notice=highword(number);
  number=lowword(number);
  assert(number>0 && number<WARN_BASE+WARN_COUNT);
  if (notice==0)
    notice=errnotice;
  errnotice=0;
  assert(notice<countof(noticemsg));

  /* errflag is reset on each semicolon.
   * In a two-pass compiler, an error should not be reported twice. Therefore
   * the error reporting is enabled only in the second pass (and only when
   * actually producing output). Fatal errors may never be ignored.
   */
  if ((errflag || sc_status!=statWRITE) && (number<FATAL_BASE || number>=WARN_BASE))
    return 0;

  /* also check for disabled warnings */
  if (WARN_ISDISABLED(number)) {
    errline=-1;
    errfile=-1;
    return 0;
  } /* if */

  const char *msg,*pre;
  if (number<FATAL_BASE){
    assert(number>0 && number<countof(errmsg));
    msg=errmsg[number];
    pre=prefix[0];
    errflag=true;       /* set errflag (skip rest of erroneous expression) */
    errcount++;
    warnignore=pc_curline;
  } else if (number<WARN_BASE) {
    assert(((int)number-FATAL_BASE)>=0 && (number-FATAL_BASE)<countof(fatalmsg));
    msg=fatalmsg[number-FATAL_BASE];
    pre=prefix[1];
    errcount++;         /* a fatal error also counts as an error */
  } else {
    assert(((int)number-WARN_BASE)>=0 && (number-WARN_BASE)<countof(warnmsg));
    if (pc_curline==warnignore)
      return 0;         /* an error was already given for this line, gobble up the (probably incorrect) warning*/
    warnignore=-1;
    msg=warnmsg[number-WARN_BASE];
    pre=prefix[2];
    warncount++;
  } /* if */

  char string[256];
  strexpand(string,(const unsigned char*)msg,sizeof string-2,SCPACK_TABLE);
  if (notice>0) {
    int len;
    assert(notice<countof(noticemsg));
    strcat(string,"; ");
    len=(int)strlen(string);
    strexpand(string+len,(unsigned char *)noticemsg[notice],sizeof string-len-1,SCPACK_TABLE);
  } /* if */
  strcat(string,"\n");

  if (errline>0)
    errstart=errline;           /* forced error position, set single line destination */
  else
    errline=pc_curline;         /* normal error, errstart may (or may not) have been marked, endpoint is current line */
  if (errstart>errline)
    errstart=errline;           /* special case: error found at end of included file */
  const char *filename;
  if (errfile>=0) {
    filename=get_inputfile(errfile);/* forced filename */
  } else {
    filename=inpfname;          /* current file */
    if (filename==NULL || strlen(filename)==0)
      filename=get_sourcefile(0);
    if (filename==NULL || strlen(filename)==0)
      filename="(none)";
  } /* if */
  assert(filename!=NULL);

  /* limit the number of errors that are reported on a single line when
   * printing the errors on the terminal (but list all when writing them to an
   * error file)
   */
  bool on_terminal= (strlen(errfname)==0);
  if ((errstart<0 && lastline!=pc_curline) || lastline<errstart || lastline>pc_curline || fcurrent!=lastfile)
    errcount_on_line=0;
  if (errcount_on_line<3 || !on_terminal) {
    if (on_terminal) {
      int start= (errstart==errline) ? -1 : errstart;
      if (pc_error((int)number,string,filename,start,errline,argptr)) {
        if (outf!=NULL) {
          pc_closeasm(outf,true);
          outf=NULL;
        } /* if */
        longjmp(errbuf,3);        /* user abort */
      } /* if */
    } else {
      FILE *fp=fopen(errfname,"a");
      if (fp!=NULL) {
        if (errstart>=0 && errstart!=errline)
          fprintf(fp,"%s(%d -- %d) : %s %03d: ",filename,errstart,errline,pre,(int)number);
        else
          fprintf(fp,"%s(%d) : %s %03d: ",filename,errline,pre,(int)number);
        vfprintf(fp,string,argptr);
        fclose(fp);
      } /* if */
    } /* if */
  } else if (number<WARN_BASE) {
    errhidden=true;
  } /* if */
  if (number<WARN_BASE || errcount_on_line>0)
    errcount_on_line++; /* don't count warnings, unless an error was also flagged */

  if (number>=FATAL_BASE && number<WARN_BASE || errcount>25){
    if (on_terminal)
      pc_error(0,"\nCompilation aborted.",NULL,0,0,argptr);
    if (outf!=NULL) {
      pc_closeasm(outf,true);
      outf=NULL;
    } /* if */
    longjmp(errbuf,2);          /* fatal error, quit */
  } /* if */

  errline=-1;                   /* reset forced file/line position */
  errfile=-1;
  lastline=pc_curline;
  lastfile=fcurrent;

  return 0;
}

/** error() - wrapper function to output an error/warning message for the
 *  current file/line postion.
 *
 *  \param number       The error/warning number. The upper half of the value
 *                      may contain a 'notice' number to show extra information.
 *  \param ...          Variable argument list for parameters of the
 *                      error/warning message.
 *
 *  \return This function always returns 0.
 *
 *  \note See `error_onpos()` for a variant that gives an error message for a
 *        specific location (other than the 'current' file/line).
 *
 *  \note See `error_v()` for details.
 */
SC_FUNC int error(unsigned long number,...)
{
  va_list argptr;
  va_start(argptr,number);
  int result=error_v(number,argptr);
  va_end(argptr);
  return result;
}

/** error_onpos() - wrapper function to output an error/warning message for the
 *  current file/line postion.
 *
 *  \param filenr       The file number for the message; -1 means the current
 *                      file.
 *  \param linenr       The line number for the message; -1 means the current
 *                      line number.
 *  \param number       The error/warning number. The upper half of the value
 *                      may contain a 'notice' number to show extra information.
 *  \param ...          Variable argument list for parameters of the
 *                      error/warning message.
 *
 *  \return This function always returns 0.
 *
 *  \note See `error()` for a variant that gives an error message for the
 *        'current' file/line.
 *
 *  \note See `error_v()` for details.
 */
SC_FUNC int error_onpos(short filenr,int linenr,unsigned long number,...)
{
  errfile=filenr;
  errline=linenr;
  va_list argptr;
  va_start(argptr,number);
  int result=error_v(number,argptr);
  va_end(argptr);
  errfile=-1;           /* reset forced file/line position */
  errline=-1;
  return result;
}

SC_FUNC int error_symbolname(unsigned number,const char *name)
{
  char *str;
  int tok=tokeninfo(NULL,&str);
  unsigned long errornum= (tok>tMIDDLE && tok<=tLAST) ? (unsigned long)makelong(number,4) : number;
  return error(errornum,name);
}

SC_FUNC int error_suggest(unsigned number,const char *name,int ident)
{
  symbol *closestsym=find_closestsymbol(name,ident);
  if (closestsym!=NULL && strcmp(name,closestsym->name)!=0)
    return error(makelong(number,1),name,closestsym->name);
  return error(number,name);
}

SC_FUNC int error_suggest_list(unsigned number,const char *name,constvalue *list)
{
  assert(name!=NULL);
  assert(list!=NULL);
  if (sc_status==statWRITE) {
    constvalue *closest=NULL;
    if (strlen(name)>0) {
      int closestdist=INT_MAX;
      while (list->next!=NULL) {
        int dist;
        list=list->next;
        dist=levenshtein_distance(list->name,name);
        if (dist<closestdist && dist<=MAX_EDIT_DIST) {
          closest=list;
          closestdist=dist;
        } /* if */
      } /* while */
    } /* if */
    if (closest!=NULL && strcmp(name,closest->name)!=0)
      return error(makelong(number,1),name,closest->name);
    return error(number,name);
  } /* if */
  return 0;
}

/** errorcount() returns the error and warning counts.
 *
 *  \param errors       [out, optional] The number of errors detected (this
 *                      includes fatal errors. This parameter may be set to
 *                      `NULL`.
 *  \param warnings     [out, optional] The number of warnings detected. This
 *                      parameter may be set to `NULL`.
 *
 *  \return 0 when no errors or warnings were detected, 1 when there are
 *          warnings (but no errors), 2 when there are errors, 3 when there are
 *          errors, some of which were hidden (because there were many errors on
 *          the same line).
 */
SC_FUNC int errorcount(int *errors,int *warnings)
{
  if (errors)
    *errors=errcount;
  if (warnings)
    *warnings=warncount;
  if (errcount>0)
    return errhidden ? 3 : 2;   /* errors found -> level 2 or 3 */
  if (warncount>0)
    return 1;                   /* no errors, but warnings found -> level 1 */
  return 0;                     /* all is fine -> 0 */
}

SC_FUNC void errorset(unsigned long code)
{
  switch (lowword(code)) {
  case sRESET:
    errflag=false;      /* start reporting errors */
    break;
  case sFORCESET:
    errflag=true;       /* stop reporting errors */
    break;
  case sEXPRMARK:
    errstart=pc_curline;/* save start line number */
    break;
  case sEXPRRELEASE:
    errstart=-1;        /* forget start line number */
    errline=-1;
    errfile=-1;
    break;
  case sRESETERRCOUNT:
    errcount=0;
    warncount=0;
    errflag=false;
    errhidden=false;
    errnotice=0;
    break;
  case sSETNOTICE:
    errnotice=highword(code);
    break;
  } /* switch */
}

/** pc_enablewarning() enables or disables a warning (errors cannot be
 *  disabled). Initially all warnings are enabled.
 *
 *  \param number       The warning number.
 *  \param enable       One of the following:
 *                      *  0 for disable (wDISABLE)
 *                      *  1 for enable (wENABLE)
 *                      *  2 for toggle (wTOGGLE)
 *
 *  \return `true` on success, `false` on a parameter error.
 *
 *  \note In the `warndisable` structure, a zero flag means 'enabled' and a
 *        non-zero means 'disabled'. This is the inverse of the `enable`
 *        parameter (0 = disable, 1 = enable).
 */
#if defined __cplusplus
  extern "C"
#endif
DLLEXPORT
int pc_enablewarning(unsigned number,int enable)
{
  if (number<WARN_BASE || number>=WARN_BASE+WARN_COUNT)
    return false;       /* errors and fatal errors cannot be disabled */

  if (enable==wTOGGLE)
    enable= WARN_ISDISABLED(number) ? wENABLE : wDISABLE;
  if (enable==wDISABLE)
    warndisable.flag[WARN_IDX(number)] |= WARN_MASK(number);
  else
    warndisable.flag[WARN_IDX(number)] &= ~WARN_MASK(number);

  return true;
}

/* pushwarnings()
 * Saves currently disabled warnings, used to implement #pragma warning push
 */
SC_FUNC void pushwarnings(void)
{
  warnstack *p=(warnstack*)malloc(sizeof(warnstack));
  if (p!=NULL) {
    memcpy(p->flag,warndisable.flag,sizeof(warndisable.flag));
    p->next=warndisable.next;
    warndisable.next=p;
  } else {
    error(103); /* insufficient memory */
  }
}

/* popwarnings()
 * This function is the reverse of pc_pushwarnings()
 */
SC_FUNC void popwarnings(void)
{
  if (warndisable.next!=NULL) {
    warnstack *p=warndisable.next;
    warndisable.next=p->next;
    memcpy(warndisable.flag,p->flag,sizeof(warndisable.flag));
    free(p);
  } else {
    error(98);          /* #pragma warning pop without push */
  }
}

/* clear_warningstack()
 * Removes any remaining stacked warning lists and cleans up the global array
 */
SC_FUNC void clear_warningstack(void)
{
  if (warndisable.next!=NULL)
    error(97);          /* #pragma warning push without pop */
  while (warndisable.next!=NULL) {
    warnstack *p=warndisable.next;
    warndisable.next=p->next;
    free(p);
  }
  memset(&warndisable,0,sizeof(warndisable));
}

/* Implementation of Levenshtein distance, by Lorenzo Seidenari
 */
static int minimum(int a,int b,int c)
{
  int min=a;
  if (b<min)
    min=b;
  if (c<min)
    min=c;
  return min;
}

SC_FUNC int levenshtein_distance(const char *s,const char *t)
{
  //Step 1
  int k,i,j,cost,distance;
  int *d;
  int n=(int)strlen(s);
  int m=(int)strlen(t);
  /* note: an empty string is valid input, the distance is then the other's length */
  d=(int*)alloca((sizeof(int))*(m+1)*(n+1));
  m++;
  n++;
  //Step 2
  for (k=0;k<n;k++)
    d[k]=k;
  for (k=0;k<m;k++)
    d[k*n]=k;
  //Step 3 and 4
  for (i=1;i<n;i++) {
    for (j=1;j<m;j++) {
      //Step 5
      cost= (tolower(s[i-1])!=tolower(t[j-1]));
      //Step 6
      d[j*n+i]=minimum(d[(j-1)*n+i]+1,d[j*n+i-1]+1,d[(j-1)*n+i-1]+cost);
    } /* for */
  } /* for */
  distance=d[n*m-1];
  return distance;
}

static int find_closestsymbol_table(const char *name,const symbol *root,int symboltype,symbol **closestsym)
{
  char symname[2*sNAMEMAX+16];

  assert(closestsym!=NULL);
  *closestsym=NULL;
  int closestdist=INT_MAX;

  assert(name!=NULL);
  int critdist=(int)strlen(name)/2; /* for short names, allow only a single edit */
  if (critdist==0)
    return closestdist;             /* a (nearly) empty name has no nearest match to offer */
  if (critdist>MAX_EDIT_DIST)
    critdist=MAX_EDIT_DIST;
  symbol *sym=root->next;
  while (sym!=NULL) {
    funcdisplayname(symname,countof(symname),sym->name);
    if (strlen(symname)>0) {
      int ident=sym->ident;
      if (symboltype==iARRAY && ident==iREFARRAY)
        ident=iARRAY;           /* reference arrays match arrays */
      else if (symboltype==iVARIABLE && (sym->ident==iCONSTEXPR || sym->ident==iREFERENCE || sym->ident==iARRAY || sym->ident==iREFARRAY))
        ident=iVARIABLE;        /* when requesting variables, constants are also ok */
      if (symboltype==ident || (symboltype==iVARIABLE && ident==iFUNCTN)) {
        int dist=levenshtein_distance(name,symname);
        if (dist<closestdist && dist<=critdist) {
          *closestsym=sym;
          closestdist=dist;
        } /* if */
      } /* if */
    } /* while */
    sym=sym->next;
  } /* while */
  return closestdist;
}

SC_FUNC symbol *find_closestsymbol(const char *name,int symboltype)
{
  symbol *symloc,*symglb;
  int distloc,distglb;

  if (sc_status==statBROWSE)
    return NULL;
  assert(name!=NULL);
  if (strlen(name)==0)
    return NULL;
  distloc=find_closestsymbol_table(name,&loctab,symboltype,&symloc);
  distglb=find_closestsymbol_table(name,&glbtab,symboltype,&symglb);
  return (distglb<distloc) ? symglb : symloc;
}

#undef SCPACK_TABLE
