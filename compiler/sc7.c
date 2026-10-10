/*  Pawn compiler - Staging buffer and optimizer
 *
 *  The staging buffer
 *  ------------------
 *  The staging buffer allows buffered output of generated code, deletion
 *  of redundant code, optimization by a tinkering process and reversing
 *  the ouput of evaluated expressions (which is used for the reversed
 *  evaluation of arguments in functions).
 *  Initially, stgwrite() writes to the file directly, but after a call to
 *  stgset(true), output is redirected to the buffer. After a call to
 *  stgset(false), stgwrite()'s output is directed to the file again. Thus
 *  only one routine is used for writing to the output, which can be
 *  buffered output or direct output.
 *
 *  staging buffer variables:   stgbuf  - the buffer
 *                              stgidx  - current index in the staging buffer
 *                              staging - if true, write to the staging buffer;
 *                                        if false, write to file directly.
 *
 * The peephole optimizer uses a dual "pipeline". The staging buffer (described
 * above) gets optimized for each expression or sub-expression in a function
 * call. The peephole optimizer is recursive, but it does not span multiple
 * sub-expressions. However, the data gets written to a second buffer that
 * behaves much like the staging buffer. This second buffer gathers all
 * optimized strings from the staging buffer for a complete expression. The
 * peephole optmizer then runs over this second buffer to find optimzations
 * across function parameter boundaries.
 *
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
 *  Version: $Id: sc7.c 7709 2026-10-10 19:44:52Z thiadmer $
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>     /* for atoi() */
#include <string.h>
#include <ctype.h>
#if defined FORTIFY
# include <alloc/Fortify.h>
#endif
#include "sc.h"

#if defined _MSC_VER
# pragma warning(push)
# pragma warning(disable:4125)  /* decimal digit terminates octal escape sequence */
#endif

#if defined PAWN_LIGHT
# if !defined AMX_NO_PACKED_OPC
#   define AMX_NO_PACKED_OPC
# endif
# if !defined AMX_NO_MACRO_INSTR
#   define AMX_NO_MACRO_INSTR
# endif
#endif
#include "sc7.scp"

#if defined _MSC_VER
# pragma warning(pop)
#endif

static int stgstring(char *start,char *end);
static void stgopt(char *start,char *end,int packed_opc,bool (*outputfunc)(char *str));


#define sSTG_GROW   512
#define sSTG_MAX    20480

static char *stgbuf=NULL;
static unsigned stgmax=0;   /* current size of the staging buffer */

static char *stgpipe=NULL;
static unsigned pipemax=0;  /* current size of the stage pipe, a second staging buffer */
static unsigned pipeidx=0;

#define CHECK_STGBUFFER(index) if ((index)>=stgmax)  grow_stgbuffer(&stgbuf, &stgmax, (index)+1)
#define CHECK_STGPIPE(index)   if ((index)>=pipemax) grow_stgbuffer(&stgpipe, &pipemax, (index)+1)

static void grow_stgbuffer(char **buffer, unsigned *curmax, unsigned requiredsize)
{
  char *p;
  int clear= (*buffer==NULL); /* if previously none, empty buffer explicitly */

  assert(curmax!=NULL);
  if (*curmax>=requiredsize)
    return;                   /* nothing to do, already sufficient space */
  /* if the staging buffer (holding intermediate code for one line) grows
   * over a few kBytes, there is probably a run-away expression
   */
  if (requiredsize>sSTG_MAX)
    error(102,"staging buffer");    /* staging buffer overflow (fatal error) */
  *curmax=requiredsize+sSTG_GROW;
  if (*buffer!=NULL)
    p=(char *)realloc(*buffer,*curmax*sizeof(char));
  else
    p=(char *)malloc(*curmax*sizeof(char));
  if (p==NULL)
    error(102,"staging buffer");    /* staging buffer overflow (fatal error) */
  *buffer=p;
  if (clear)
    **buffer='\0';
}

SC_FUNC void stgbuffer_cleanup(void)
{
  if (stgbuf!=NULL) {
    free(stgbuf);
    stgbuf=NULL;
    stgmax=0;
  } /* if */
  if (stgpipe!=NULL) {
    free(stgpipe);
    stgpipe=NULL;
    pipemax=0;
    pipeidx=0;
  } /* if */
}

/* the variables "stgidx" and "staging" are declared in "scvars.c" */

/*  stgmark
 *
 *  Copies a mark into the staging buffer. At this moment there are three
 *  possible marks:
 *     sSTARTREORDER    identifies the beginning of a series of expression
 *                      strings that must be written to the output file in
 *                      reordered order
 *    sENDREORDER       identifies the end of 'reverse evaluation'
 *    sEXPRSTART + idx  only valid within a block that is evaluated in
 *                      reordered order, it identifies the start of an
 *                      expression; the "idx" value is the argument position
 *
 *  Global references: stgidx  (modified)
 *                     stgbuf  (modified)
 *                     staging (read-only)
 */
SC_FUNC void stgmark(char mark)
{
  if (staging) {
    CHECK_STGBUFFER(stgidx);
    stgbuf[stgidx++]=mark;
  } /* if */
}

static bool rebuffer(char *str)
{
  if (sc_status==statWRITE) {
    assert(pipeidx==0 || stgpipe!=NULL);
    if (pipeidx>=2 && stgpipe[pipeidx-1]=='\0' && stgpipe[pipeidx-2]!='\n')
      pipeidx-=1;                      /* overwrite last '\0' */
    while (*str!='\0') {               /* copy to staging buffer */
      CHECK_STGPIPE(pipeidx);
      stgpipe[pipeidx++]=*str++;
    } /* while */
    CHECK_STGPIPE(pipeidx);
    stgpipe[pipeidx++]='\0';
  } /* if */
  return true;
}

static bool filewrite(char *str)
{
  if (sc_status==statWRITE)
    return pc_writeasm(outf,str);
  return true;
}

/*  stgwrite
 *
 *  Writes the string "st" to the staging buffer or to the output file. In the
 *  case of writing to the staging buffer, the terminating byte of zero is
 *  copied too, but... the optimizer can only work on complete lines (not on
 *  fractions of it. Therefore if the string is staged, if the last character
 *  written to the buffer is a '\0' and the previous-to-last is not a '\n',
 *  the string is concatenated to the last string in the buffer (the '\0' is
 *  overwritten). This also means an '\n' used in the middle of a string isn't
 *  recognized and could give wrong results with the optimizer.
 *  Even when writing to the output file directly, all strings are buffered
 *  until a whole line is complete.
 *
 *  Global references: stgidx  (modified)
 *                     stgbuf  (modified)
 *                     staging (read-only)
 */
SC_FUNC void stgwrite(const char *st)
{
  if (staging) {
    assert(stgidx==0 || stgbuf!=NULL);  /* staging buffer must be valid if there is (apparently) something in it */
    if (stgidx>=2 && stgbuf[stgidx-1]=='\0' && stgbuf[stgidx-2]!='\n')
      stgidx-=1;                       /* overwrite last '\0' */
    while (*st!='\0') {                /* copy to staging buffer */
      CHECK_STGBUFFER(stgidx);
      stgbuf[stgidx++]=*st++;
    } /* while */
    CHECK_STGBUFFER(stgidx);
    stgbuf[stgidx++]='\0';
  } else {
    unsigned len=(stgbuf!=NULL) ? strlen(stgbuf) : 0;
    CHECK_STGBUFFER(len+(int)strlen(st)+1);
    strcat(stgbuf,st);
    len=strlen(stgbuf);
    if (len>0 && stgbuf[len-1]=='\n') {
      filewrite(stgbuf);
      stgbuf[0]='\0';
    } /* if */
  } /* if */
}

/** stgout() writes the staging buffer to the output file via stgstring() (for
 *  reversing expressions in the buffer) and stgopt() (for optimizing). It
 *  resets "stgidx".
 *
 *  \param index        The starting point in the staging buffer.
 *
 *  Global references: stgidx  (modified)
 *                     stgbuf  (read-only)
 *                     staging (read-only)
 */
SC_FUNC void stgout(unsigned index)
{
  if (!staging)
    return;
  assert(pipeidx==0);

  /* first pass: sub-expressions */
  assert(index<=stgidx);
  if (sc_status==statWRITE)
    stgstring(&stgbuf[index],&stgbuf[stgidx]);
  stgidx=index;

  /* second pass: optimize the buffer created in the first pass (always, even
     if the first pass did not re-order sub-instructions, because the first
     pass stops at level sOPTIMIZE_MACRO);
     skip the seconf pass when the pipe is empty, which happens on
     error-recovery, in which case the first pass staged nothing and "stgpipe"
     was never allocated */
  if (sc_status==statWRITE && pipeidx>0) {
    assert(stgpipe!=NULL);
    stgopt(stgpipe,stgpipe+pipeidx,true,filewrite);
  }

  pipeidx=0;  /* reset second pipe */
}

typedef struct {
  char *start,*end;
} argstack;

/** stgstring() analyses whether code strings should be output to the file as
 *  they appear in the staging buffer or whether portions of it should be
 *  re-ordered. Re-ordering takes place in function argument lists; Pawn passes
 *  arguments to functions from right to left. When arguments are "named" rather
 *  than positional, the order in the source stream is indeterminate.
 *  This function calls itself recursively in case it needs to re-order code
 *  strings, and it uses a private stack (or list) to mark the start and the
 *  end of expressions in their correct (reversed) order.
 *  In any case, stgstring() sends a block as large as possible to the
 *  optimizer stgopt().
 *
 *  In "reorder" mode, each set of code strings must start with the token
 *  sEXPRSTART, even the first. If the token sSTARTREORDER is represented
 *  by '[', sENDREORDER by ']' and sEXPRSTART by '|' the following applies:
 *     '[]...'     valid, but useless; no output
 *     '[|...]     valid, but useless; only one string
 *     '[|...|...] valid and usefull
 *     '[...|...]  invalid, first string doesn't start with '|'
 *     '[|...|]    invalid
 */
static int stgstring(char *start,char *end)
{
  char *ptr;
  int nest,argc,arg;
  argstack *stack;
  int reordered=0;

  while (start<end) {
    if (*start==sSTARTREORDER) {
      start+=1;         /* skip token */
      /* allocate a argstack with sMAXARGS items */
      stack=(argstack *)malloc(sMAXARGS*sizeof(argstack));
      if (stack==NULL)
        error(103);     /* insufficient memory */
      reordered=1;      /* mark that the expression is reordered */
      nest=1;           /* nesting counter */
      argc=0;           /* argument counter */
      arg=-1;           /* argument index; no valid argument yet */
      do {
        switch (*start) {
        case sSTARTREORDER:
          nest++;
          start++;
          break;
        case sENDREORDER:
          nest--;
          start++;
          break;
        default:
          if ((*start & sEXPRSTART)==sEXPRSTART) {
            if (nest==1) {
              if (arg>=0)
                stack[arg].end=start-1; /* finish previous argument */
              arg=(unsigned char)*start - sEXPRSTART;
              stack[arg].start=start+1;
              if (arg>=argc)
                argc=arg+1;
            } /* if */
            start++;
          } else {
            start+=strlen(start)+1;
          } /* if */
        } /* switch */
      } while (nest); /* enddo */
      if (arg>=0)
        stack[arg].end=start-1;   /* finish previous argument */
      while (argc>0) {
        argc--;
        stgstring(stack[argc].start,stack[argc].end);
      } /* while */
      free(stack);
    } else {
      ptr=start;
      while (ptr<end && *ptr!=sSTARTREORDER)
        ptr+=strlen(ptr)+1;
      stgopt(start,ptr,false,rebuffer);
      start=ptr;
    } /* if */
  } /* while (start<end) */
  return reordered;
}

/*  stgdel
 *
 *  Scraps code from the staging buffer by resetting "stgidx" to "index".
 *
 *  Global references: stgidx (modified)
 *                     staging (reffered to only)
 */
SC_FUNC void stgdel(unsigned index,cell code_index)
{
  if (staging) {
    stgidx=index;
    code_idx=code_index;
  } /* if */
}

SC_FUNC int stgget(unsigned *index,cell *code_index)
{
  if (staging) {
    *index=stgidx;
    *code_index=code_idx;
  } /* if */
  return staging;
}

/** stgset() set staging on or off. If it's turned on, the routine makes sure
 *  the index (`stgidx`) is set to 0 (it should already be 0). In either case,
 *  the staging buffer (`stgbuf`) is cleared.
 *
 *  Global references: staging  (modified)
 *                     stgidx   (modified)
 *                     stgbuf   (contents modified)
 */
SC_FUNC void stgset(bool onoff)
{
  if (onoff!=staging) {
    staging=onoff;
    if (staging) {
      assert(stgidx==0);
      stgidx=0;
      CHECK_STGBUFFER(stgidx);
      /* write any contents that may be put in the buffer by stgwrite()
       * when "staging" was 0
       */
      if (strlen(stgbuf)>0)
        filewrite(stgbuf);
    } /* if */
    *stgbuf='\0';
  }
}


static SEQUENCE *sequences; /**< list of peephole optimzation sequences */

/** phopt_init() initializes all sequence strings of the peehole optimizer. The
 *  strings are embedded in the executable in compressed format, here we expand
 *  them (and allocate memory for the sequences).
 */
SC_FUNC int phopt_init(void)
{
  /* count number of sequences */
  int count;
  for (count=0; sequences_cmp[count].find!=NULL; count++)
    /* nothing */;
  count++;             /* include an item for the NULL terminator */

  if ((sequences=(SEQUENCE*)malloc(count * sizeof(SEQUENCE)))==NULL)
    return false;

  /* pre-initialize all to NULL (in case of failure) */
  for (int i=0; i<count; i++) {
    sequences[i].find=NULL;
    sequences[i].replace=NULL;
    sequences[i].opc=0;
    sequences[i].arg=0;
  } /* for */

  /* expand all strings */
  for (int i=0; i<count-1; i++) {
    char str[160];
    unsigned len = strexpand(str,(unsigned char*)sequences_cmp[i].find,sizeof str,SCPACK_TABLE);
    assert(len<=sizeof str);
    assert(len==strlen(str)+1);
    sequences[i].find=(char*)malloc(len);
    if (sequences[i].find!=NULL)
      strcpy((char*)sequences[i].find,str);
    len = strexpand(str,(unsigned char*)sequences_cmp[i].replace,sizeof str,SCPACK_TABLE);
    assert(len<=sizeof str);
    assert(len==strlen(str)+1);
    sequences[i].replace=(char*)malloc(len);
    if (sequences[i].replace!=NULL)
      strcpy((char*)sequences[i].replace,str);
    sequences[i].opc=sequences_cmp[i].opc;
    sequences[i].arg=sequences_cmp[i].arg;
    if (sequences[i].find==NULL || sequences[i].replace==NULL)
      return phopt_cleanup();
  } /* for */

  return true;
}

SC_FUNC int phopt_cleanup(void)
{
  if (sequences!=NULL) {
    int i=0;
    while (sequences[i].find!=NULL || sequences[i].replace!=NULL) {
      if (sequences[i].find!=NULL)
        free((char*)sequences[i].find);
      if (sequences[i].replace!=NULL)
        free((char*)sequences[i].replace);
      i++;
    } /* while */
    free(sequences);
    sequences=NULL;
  } /* if */
  return false;
}

#define MAX_OPT_VARS    5
#define MAX_OPT_CAT     5       /* max. values that are concatenated */
#if sNAMEMAX > (PAWN_CELL_SIZE/4) * MAX_OPT_CAT
# define MAX_ALIAS       sNAMEMAX
#else
# define MAX_ALIAS       (PAWN_CELL_SIZE/4) * MAX_OPT_CAT
#endif
#define MAX_ASM_INSTR   128     /* max. length of a full assembler instruction line */

/** matchsequence() checks whether the pseudo-assembly code between `start` and
 *  `end`  matches a pattern; it collects the parameters of the instructions in
 *  the process.
 *
 *  \param start        [in] The start of the assembly code.
 *  \param end          [in] Points just behind the end of the assembly code.
 *  \param pattern      [in] The 'match' pattern to test against.
 *  \param symbols      [out] Set the to collected parameters of the assembly
 *                      instructions.
 *
 *  \return The length of the section in the source code, starting from `start`
 *          that matches the pattern, or 0 on no match.
 */
static size_t matchsequence(const char *start,const char *end,const char *pattern,
                            char symbols[MAX_OPT_VARS+1][MAX_ALIAS+1])
{
  assert(start!=NULL && end!=NULL);
# if !defined NDEBUG
    int optsym=false; /* set to true if a parameter may optionally be present (macro instructions) */
# endif

  char str[MAX_ALIAS+1];
  cell value;
  char *ptr;

  bool chk_seqpoint=true;
  bool start_sym=true;
  const char *start_reset=NULL;
  assert(symbols!=NULL);
  for (int var=0; var<=MAX_OPT_VARS; var++)
    symbols[var][0]='\0';

  const char *start_org=start;
  while (*start=='\t' || *start==' ')
    start++;
  while (*pattern) {
    if (start>=end)
      return 0;
    if (chk_seqpoint && (strcmp(pattern,"%seq!")==0 || strcmp(pattern,"%pri!")==0 || strcmp(pattern,"%alt!")==0)) {
      /* check the source line for a sequence point (instruction or comment) */
      const char *sptr=start;
      while (sptr<end && (*sptr=='\t' || *sptr==' '))
        sptr++; /* skip leading whitespace (likely already skipped) */
      unsigned reg=0;
      if (strcmp(pattern,"%pri!")==0)
        reg=(1<<sPRI);
      else if (strcmp(pattern,"%alt!")==0)
        reg=(1<<sALT);
      else
        reg=(1<<sPRI) | (1<<sALT);  /* %seq! */
      if (!is_sequence_point(sptr,reg))
        return 0;
      /* unskip whitespace skipped by the last '!' */
      if (start_reset!=NULL)
        start=start_reset;
      break;    /* the pattern ended, the squence point is not included in the match length */
    } else {
      chk_seqpoint=false;
    } /* if */
    switch (*pattern) {
    case '%':   /* new "symbol" */
      pattern++;
      assert(isdigit(*pattern));
      int var=atoi(pattern);
      assert(var>=0 && var<=MAX_OPT_VARS);
      assert(*start=='-' || alphanum(*start) || optsym);
      int i;
      for (i=0; start<end && (*start=='-' || *start=='+' || alphanum(*start)); i++,start++) {
        assert(i<=MAX_ALIAS);
        str[i]=*start;
      } /* for */
      str[i]='\0';
      if (var==0) {
        /* match only if the parameter is numeric and in the range of a half cell */
        const char *varptr;
        /* getparamvalue() resolves leading '-' on values and adds multiple
           values (the peephole optimizer may create such variants) */
        ucell v=getparamvalue(str,&varptr);
        /* sign-extend the value to check whether the value is in the range for
           packed parameters */
        ucell v2=v;
        if ((v2 & ((ucell)1<<pc_cellsize*8-1))!=0 && (size_t)pc_cellsize<sizeof(ucell))
          v2|=~(ucell)0U<<pc_cellsize*8;
        if (*varptr>' ' || v2>=((ucell)1<<((pc_cellsize*4)-1)) && v2<=~((ucell)1<<((pc_cellsize*4)-1)))
          return 0;
        /* reconvert the value to a string (without signs or expressions) */
        varptr=celltohex(v);
#       if !defined NDEBUG
          assert(strlen(varptr)==2*(size_t)pc_cellsize);
          assert((varptr[0]=='0' || varptr[0]=='f') && (varptr[1]=='0' || varptr[1]=='f'));
          if (pc_cellsize>=32)
            assert((varptr[2]=='0' || varptr[2]=='f') && (varptr[3]=='0' || varptr[3]=='f'));
          if (pc_cellsize>=64) {
            assert((varptr[4]=='0' || varptr[4]=='f') && (varptr[5]=='0' || varptr[5]=='f'));
            assert((varptr[6]=='0' || varptr[6]=='f') && (varptr[7]=='0' || varptr[7]=='f'));
          } /* if */
#       endif
        if (v==0) {
          str[0]='0'; /* make zero transform to '0' rather than '0000' */
          str[1]='\0';
        } else {
          memmove(str,varptr+pc_cellsize,pc_cellsize+1);
        } /* if */
      } /* if */
      if (symbols[var][0]!='\0') {
        if (strcmp(symbols[var],str)!=0)
          return 0; /* symbols should be identical */
      } else {
        strcpy(symbols[var],str);
      } /* if */
      start_sym=false;
#     if !defined NDEBUG
        optsym=false;
#     endif
      break;
    case '-':
      value=-hex2cell(pattern+1,&pattern);
      ptr=celltohex((ucell)value);
      while (*ptr!='\0') {
        if (tolower(*start) != tolower(*ptr))
          return 0;
        start++;
        ptr++;
      } /* while */
      pattern--;  /* there is an increment following at the end of the loop */
      start_sym=true;
      break;
    case '+':
      value=hex2cell(pattern+1,&pattern);
      ptr=celltohex((ucell)value);
      while (*ptr!='\0') {
        if (tolower(*start) != tolower(*ptr))
          return 0;
        start++;
        ptr++;
      } /* while */
      pattern--;  /* there is an increment following at the end of the loop */
      break;
    case ' ':     /* required whitespace */
      if (*start!='\t' && *start!=' ')
        return 0;
      while (start<end && (*start=='\t' || *start==' '))
        start++;
      start_sym=true;
      break;
    case '~':     /* optional whitespace (followed by optional symbol) */
      while (start<end && (*start=='\t' || *start==' '))
        start++;
      start_sym=true;
#     if !defined NDEBUG
        optsym= (pattern[1]=='%');
#     endif
      break;
    case '!':
      while (start<end && (*start=='\t' || *start==' '))
        start++;                /* skip trailing white space */
      if (*start==';')
        while (start<end && *start!='\n')
          start++;              /* skip trailing comment */
      if (*start!='\n')
        return 0;
      assert(*(start+1)=='\0');
      start+=2;                 /* skip '\n' and '\0' */
      start_reset=start;        /* keep reset point (for sequence points) */
      if (*(pattern+1)!='\0')
        while (start<end && *start=='\t' || *start==' ')
          start++;              /* skip leading white space of next instruction */
      start_sym=true;
      chk_seqpoint=true;
      break;
    default:
      if (start_sym && isdigit(*pattern) && *start=='0') {
        /* for numeric parameters, skip leading zeros */
        while (*pattern=='0' && isdigit(*(pattern+1)))
          pattern++;
        while (*start=='0' && isdigit(*(start+1)))
          start++;
      }
      if (tolower(*start) != tolower(*pattern))
        return 0;
      start++;
      start_sym=false;
    } /* switch */
    pattern++;
  } /* while */

  return (size_t)(start-start_org);
}

/** replacesequence() creates a set of pseudo-assembly instructions from a
 *  pattern and an array of symbols (for the parameters of the assembly
 *  instructions).
 *
 *  \param pattern      [in] The 'replacement' pattern.
 *  \param symbols      [in] An array of symbols to use in replacement.
 *  \param repl_length  [out] The total length of the replacement after
 *                      substituting the symbols.
 *  \param maxlength    The maximum length that is allowed for the replacement.
 *
 *  \return The data for the replacement, or `NULL` on failure.
 */
static char *replacesequence(const char *pattern,const char symbols[MAX_OPT_VARS+1][MAX_ALIAS+1],
                             size_t *repl_length,size_t maxlength)
{
# if !defined NDEBUG
    int optsym=false;
# endif

  /* calculate the length of the new buffer
   * this is the length of the pattern plus the length of all symbols (note
   * that the same symbol may occur multiple times in the pattern) plus
   * line endings and startings ('\t' to start a line and '\n\0' to end one)
   */
  assert(repl_length!=NULL);
  *repl_length=0;
  for (const char *pptr=pattern; *pptr; pptr++) {
    /* for the special characters/operators, see sc7.scp */
    switch (*pptr) {
    case '%': {         /* parameter prefix */
      pptr++;           /* skip '%' */
      assert(isdigit(*pptr));
      int var=atoi(pptr);
      assert(var>=0 && var<=MAX_OPT_VARS);
      assert(symbols[var][0]!='\0' || optsym);  /* variable should be defined */
      assert(var!=0 || strlen(symbols[var])==(size_t)pc_cellsize || (symbols[var][0]=='-' && strlen(symbols[var])==(size_t)pc_cellsize+1) || atoi(symbols[var])==0);
      *repl_length+=strlen(symbols[var]);
#     if !defined NDEBUG
        optsym=false;
#     endif
      break;
    } /* case */
    case '~': {         /* optional space followed by optional symbol */
      assert(pptr[1]=='%');
      assert(isdigit(pptr[2]));
      int var=atoi(pptr+2);
      assert(var>=0 && var<=MAX_OPT_VARS);
      if (symbols[var][0]!='\0')
        *repl_length+=1;  /* copy space if following symbol is valid */
#     if !defined NDEBUG
        else optsym=true; /* don't copy space, and symbol is optional */
#     endif
      break;
    } /* case */
    case '#':           /* literal number, to be expanded to full cell size */
      pptr++;           /* skip '#' */
      assert(alphanum(*pptr) || *pptr=='-');
      if (*pptr=='-')
        pptr++;
      while (alphanum(pptr[1]))
        pptr++;
      *repl_length+=pc_cellsize*2;
      break;
    case '!':           /* line terminator */
      *repl_length+=3;  /* '\n', '\0' & '\t' */
      break;
    case ';':           /* ';!' is fully empty replacement */
      if (*(pptr+1)=='!') {
        assert(pptr==pattern);
        pptr++;         /* skip ';' and skip '!' at the bottom of the loop */
        *repl_length+=1;/* add '\0' */
      } else {
        *repl_length+=1;/* keep ';' as a literal */
      }
      break;
    default:
      *repl_length+=1;
    } /* switch */
  } /* for */

  /* allocate a buffer to replace the sequence in */
  char *buffer=(char*)malloc(*repl_length);
  if (!buffer) {
    error(103);
    return NULL;
  } /* if */

  /* replace the pattern into this temporary buffer */
# if !defined NDEBUG
    optsym=false;
# endif
  char *sptr=buffer;
  if (!(*pattern==';' && *(pattern+1)=='!'))
    *sptr++='\t';       /* the "replace" patterns do not have tabs (but there is no \t for an empty replacement */
  while (*pattern) {
    assert((size_t)(sptr-buffer)<*repl_length);
    switch (*pattern) {
    case '%': {
      /* write out the symbol */
      pattern++;
      assert(isdigit(*pattern));
      int var=atoi(pattern);
      assert(var>=0 && var<=MAX_OPT_VARS);
      assert(symbols[var][0]!='\0' || optsym);  /* variable should be defined */
      assert(symbols[var][0]=='\0' || !optsym); /* but optional variable should be undefined */
      strcpy(sptr,symbols[var]);
      sptr+=strlen(symbols[var]);
#     if !defined NDEBUG
        optsym=false;
#     endif
      break;
    } /* case */
    case '~': {
      assert(pattern[1]=='%');
      assert(isdigit(pattern[2]));
      int var=atoi(pattern+2);
      assert(var>=0 && var<=MAX_OPT_VARS);
      if (symbols[var][0]!='\0')
        *sptr++=' ';      /* replace ~ by a space */
#     if !defined NDEBUG
        else optsym=true; /* don't copy space, and symbol is optional */
#     endif
      break;
    } /* case */
    case '#': {
      ucell v=hex2ucell(pattern+1,NULL);
      const char *ptr=celltohex(v);
      /* the literal is expanded to cell size, but if the length of the
         replacement string exceeds the maximum length, it is then shrunk by
         stripping leading zeros (until the length of the replacement string
         no longer exceeds the maximum length); see also the comment
         REPLACEMENT_RULE_FIT in stgopt() */
      size_t len=strlen(ptr);
      if (*repl_length>maxlength) {
        while (*repl_length>maxlength && *ptr=='0' && len>1) {
          ptr++;
          *repl_length-=1;
          len-=1;
        } /* while */
      } /* if */
      strcpy(sptr,ptr);
      sptr+=len;
      assert(alphanum(pattern[1]) || pattern[1]=='-');
      if (pattern[1]=='-')
        pattern++;
      while (alphanum(pattern[1]))
        pattern++;
      break;
    } /* case */
    case ';':
      if (*(pattern+1)=='!') {
        *sptr++='\0';
        pattern++;        /* skip ';', '!' is skipped at the bottom of the loop */
      } else {
        *sptr++=*pattern; /* keep ';' as a literal */
      } /* if */
      break;
    case '!':
      /* finish the line, optionally start the next line with an indent */
      *sptr++='\n';
      *sptr++='\0';
      if (*(pattern+1)!='\0')
        *sptr++='\t';
      break;
    default:
      *sptr++=*pattern;
    } /* switch */
    pattern++;
  } /* while */

  assert((size_t)(sptr-buffer)==*repl_length);
  return buffer;
}

static void strreplace(char *dest,const char *replace,int sub_length,int repl_length,int dest_length)
{
  int offset=sub_length-repl_length;
  if (offset>0) {               /* delete a section */
    memmove(dest,dest+offset,dest_length-offset);
    memset(dest+dest_length-offset,0xcc,offset); /* not needed, but to aid debuggingg */
  } else if (offset<0) {        /* insert a section */
    memmove(dest-offset,dest,dest_length);
  } /* if */
  memcpy(dest,replace,repl_length);
}

/*  stgopt
 *
 *  Optimizes the staging buffer by checking for series of instructions that
 *  can be coded more compact. The routine expects the lines in the staging
 *  buffer to be separated with '\0' characters.
 *
 *  This rules in the peephole optimizer are categorized according to the
 *  optimization level. This routine iteratively runs the rules of the lowest
 *  level first, until no more optimizations can be done (at that level). Then
 *  it goes to the next level and re-runs through the rules again (now including
 *  the ones of the higher level).
 *
 *  The longest sequences should probably be checked first, but the current
 *  implementation simply uses the order in the list.
 */

static void stgopt(char *start,char *end,int packed_opc,bool (*outputfunc)(char *str))
{
  assert(sequences!=NULL);
  assert(start!=NULL && end!=NULL && end>=start);
  /* do not match anything if optimization is off (or if not writing to output
     anyway) */
  if (pc_optimize>sOPTIMIZE_NONE && sc_status==statWRITE && start!=end) {
    int max_level=packed_opc ? pc_optimize : (pc_optimize<sOPTIMIZE_MACRO ? pc_optimize : sOPTIMIZE_MACRO);
    int opt_level=sOPTIMIZE_CORE; /* start at "core" optimizations, proceed to higher levels after these have exhausted */
    int matches;                  /* optimization is iterative, continue until no more matches are found */
    do {
      int top_seq=0;
      if (opt_level>sOPTIMIZE_CORE) {
        while (sequences[top_seq].find!=NULL && *sequences[top_seq].find!=opt_level)
          top_seq++;  /* skip optimization rules of a lower level (these were already handled) */
        if (sequences[top_seq].find!=NULL)
          top_seq++;  /* skip the separator too */
      }
      char *head=start;
      matches=0;
      while (head<end) {
        int seq=top_seq;
        assert(seq>=0);
        while (sequences[seq].find!=NULL && *sequences[seq].find>sOPTIMIZE_NUMBER) {
          char symbols[MAX_OPT_VARS+1][MAX_ALIAS+1];
          size_t match_length=matchsequence(head,end,sequences[seq].find,symbols);
          if (match_length>0) {
            size_t repl_length;
            char *replace=replacesequence(sequences[seq].replace,symbols,&repl_length,match_length);
            /* REPLACEMENT_RULE_FIT: If the replacement is bigger than the
             * original section, we would need to "grow" the staging buffer.
             * However, this is complex due to the re-ordering of expressions
             * that can also happen in the staging buffer (see also the comment
             * above stgstring()). In addition, the peephole optimizer is
             * supposed to replace sequences with *shorter* equivalents. The
             * caveat is that the criterion of "shorter" for the rules of the
             * peephole optimizer is: less instructions and/or less operands.
             * It does not rule out that the text representation of the
             * replacement may in some circumstances be longer than the text
             * that is matched. In other words, in some circumstances, some
             * optimization rules are skipped despite a match.
             */
            if (match_length>=repl_length) {
              strreplace(head,replace,match_length,repl_length,(int)(end-head));
              end-=match_length-repl_length;
              code_idx-=opcodes(sequences[seq].opc)+opargs(sequences[seq].arg);
              matches++;
            } else {
              seq++;    /* this rule does not fit, try the next one */
            } /* if */
            free(replace);
          } else {
            seq++;
          } /* if */
        } /* while */
        assert(sequences[seq].find==NULL || *sequences[seq].find<=sOPTIMIZE_NUMBER);
        head += strlen(head) + 1;         /* to next string */
      } /* while (head<end) */
    } while (matches>0 || (opt_level++<max_level));
  } /* if (pc_optimize>sOPTIMIZE_NONE && sc_status==statWRITE) */

  while (start<end) {
    outputfunc(start);
    start+=strlen(start)+1;
  }
}

#undef SCPACK_TABLE
