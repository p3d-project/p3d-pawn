/*  Pawn compiler - File input, preprocessing and lexical analysis functions
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
 *  Version: $Id: sc2.c 7706 2026-10-03 08:17:49Z thiadmer $
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "lstring.h"
#include "sc.h"
#if defined __LINUX__ || defined __FreeBSD__ || defined __OpenBSD__ || defined __APPLE__
# include <sclinux.h>
#endif

#if defined FORTIFY
# include <alloc/Fortify.h>
#endif

/* flags for litchar() */
#define RAWMODE         0x1
#define UTF8MODE        0x2
#define ISPACKED        0x4
static cell litchar(const unsigned char **lptr,int flags);
static symbol *find_symbol(const symbol *root,const char *name,int fnumber,int stateid);

static void substallpatterns(unsigned char *line,int buffersize);
static bool match(const char *keyword,bool need_nonalpha);
static bool alpha(unsigned char c);

#define SKIPMODE      1 /* bit field in "#if" stack */
#define PARSEMODE     2 /* bit field in "#if" stack */
#define HANDLED_ELSE  4 /* bit field in "#if" stack */
#define SKIPPING      (skiplevel>0 && (ifstack[skiplevel-1] & SKIPMODE)==SKIPMODE)

static short icomment;  /* currently in multiline comment? */
static char ifstack[sCOMP_STACK]; /* "#if" stack */
static short iflevel;   /* nesting level if #if/#else/#endif */
static short skiplevel; /* level at which we started skipping (including nested #if .. #endif) */
static unsigned char term_expr[] = "";
static int listline=-1; /* "current line" for the list file */


/*  pushstk & popstk
 *
 *  Uses a LIFO stack to store information. The stack is used by doinclude(),
 *  doswitch() (to hold the state of "swactive") and some other routines.
 *
 *  Porting note: I made the bold assumption that an integer will not be
 *  larger than a pointer (it may be smaller). That is, the stack element
 *  is typedef'ed as a pointer type, but I also store integers on it. See
 *  SC.H for "stkitem"
 *
 *  Global references: stack,stkidx,stktop (private to pushstk(), popstk()
 *                     and clearstk())
 */
static stkitem *stack=NULL;
static int stkidx=0,stktop=0;

SC_FUNC void pushstk(stkitem val)
{
  assert(stkidx<=stktop);
  if (stkidx==stktop) {
    stkitem *newstack;
    int newsize= (stktop==0) ? 16 : 2*stktop;
    /* try to resize the stack */
    assert(newsize>stktop);
    newstack=(stkitem*)malloc(newsize*sizeof(stkitem));
    if (newstack==NULL)
      error(102,"parser stack");  /* stack overflow (recursive include?) */
    /* swap the stacks */
    if (stack!=NULL) {
      memcpy(newstack,stack,stkidx*sizeof(stkitem));
      free(stack);
    }
    stack=newstack;
    stktop=newsize;
  } /* if */
  assert(stkidx<stktop);
  stack[stkidx]=val;
  stkidx+=1;
}

SC_FUNC stkitem popstk(void)
{
  if (stkidx==0) {
    stkitem s;
    s.i=-1;             /* stack is empty */
    return s;
  } /* if */
  stkidx--;
  assert(stack!=NULL);
  return stack[stkidx];
}

SC_FUNC void clearstk(void)
{
  assert(stack!=NULL || stktop==0);
  if (stack!=NULL) {
    free(stack);
    stack=NULL;
    stktop=0;
  } /* if */
  assert(stktop==0);
}

SC_FUNC bool plungequalifiedfile(const char *name)
{
static const char *extensions[] = { ".i", ".inc", ".p", ".pawn" };
  FILE *fp;
  char *ext;
  unsigned ext_idx;

  assert(name!=NULL);
  ext_idx=0;
  do {
    fp=(FILE*)pc_opensrc(name);
    ext=strchr(name,'\0');      /* save position */
    if (fp==NULL) {
      /* try to append an extension */
      strcpy(ext,extensions[ext_idx]);
      fp=(FILE*)pc_opensrc(name);
      if (fp==NULL)
        *ext='\0';              /* on failure, restore filename */
    } /* if */
    ext_idx++;
  } while (fp==NULL && ext_idx<countof(extensions));
  if (fp==NULL) {
    *ext='\0';                  /* restore filename */
    return false;
  } /* if */
  PUSHSTK_P(inpf);
  PUSHSTK_P(inpfname);          /* pointer to current file name */
  PUSHSTK_P(curlibrary);
  PUSHSTK_I(iflevel);
  assert(!SKIPPING);
  assert(skiplevel==iflevel);   /* these two are always the same when "parsing" */
  PUSHSTK_I(sc_is_utf8);
  PUSHSTK_I(icomment);
  PUSHSTK_I(fcurrent);
  PUSHSTK_I(pc_curline);
  inpfname=duplicatestring(name);/* set name of include file */
  if (inpfname==NULL)
    error(103);                 /* insufficient memory */
  inpf=fp;                      /* set input file pointer to include file */
  fnumber++;
  pc_curline=0;                 /* set current line number to 0 */
  fcurrent=fnumber;
  icomment=0;                   /* not in a comment */
  insert_dbgfile(inpfname);     /* attach to debug information */
  insert_inputfile(inpfname);   /* save for the error system */
  assert(sc_status==statBROWSE || strcmp(get_inputfile(fcurrent),inpfname)==0);
  setfiledirect(inpfname);      /* (optionally) set in the list file */
  listline=-1;                  /* force a #line directive when changing the file */
  sc_is_utf8=(short)scan_utf8(inpf,name);
  return true;
}

SC_FUNC bool plungefile(char *name,int try_currentpath,int try_includepaths)
{
  bool result=false;

  if (try_currentpath) {
    /* try to open the file in the same directory as the current file --but
     * first check whether there is a (relative) path for the current file
     */
    const char *ptr;
    if ((ptr=strrchr(inpfname,DIRSEP_CHAR))!=0) {
      int len=(int)(ptr-inpfname)+1;
      if (len+strlen(name)<_MAX_PATH) {
        char path[_MAX_PATH];
        strlcpy(path,inpfname,len+1);
        strlcat(path,name,countof(path));
        result=plungequalifiedfile(path);
      } /* if */
    } else {
      /* there is no path for the current source file, meaning that it comes
       * from the active directory; try to open the file from the active
       * directory too
       */
      result=plungequalifiedfile(name);
    } /* if */
  } /* if */

  if (try_includepaths && name[0]!=DIRSEP_CHAR) {
    int i;
    const char *ptr;
    for (i=0; !result && (ptr=get_path(i))!=NULL; i++) {
      char path[_MAX_PATH];
      strlcpy(path,ptr,countof(path));
      strlcat(path,name,countof(path));
      result=plungequalifiedfile(path);
    } /* while */
  } /* if */

  return result;
}

static void check_empty(const unsigned char *lptr)
{
  /* verifies that the string contains only whitespace */
  while (*lptr<=' ' && *lptr!='\0')
    lptr++;
  if (*lptr!='\0')
    error(38);          /* extra characters on line */
}

/** doinclude() gets the name of an include file, pushes the old file on the
 *  stack and sets some options. This routine doesn't use lex(), since lex()
 *  doesn't recognize filenames (and directories).
 *
 *  Global references: inpf     (modified)
 *                     inpfname (modified)
 *                     lexptr   (modified)
 */
static void doinclude(bool silent)
{
  char name[_MAX_PATH];
  char symname[sNAMEMAX];
  char *ptr;

  while (*lexptr<=' ' && *lexptr!='\0')     /* skip leading whitespace */
    lexptr++;
  char c='\0';
  if (*lexptr=='<' || *lexptr=='\"') {
    c=(char)((*lexptr=='\"') ? '\"' : '>'); /* termination character */
    lexptr++;
    while (*lexptr<=' ' && *lexptr!='\0')   /* skip whitespace after quote */
      lexptr++;
  } /* if */

  int i=0;
  while (*lexptr!=c && *lexptr!='\0' && i<(int)countof(name)-1)  /* find the end of the string */
    name[i++]=*lexptr++;
  while (i>0 && name[i-1]<=' ')
    i--;                        /* strip trailing whitespace */
  assert(i>=0 && i<(int)countof(name));
  name[i]='\0';                 /* zero-terminate the string */

  if (*lexptr!=c) {             /* verify correct string termination */
    error(37);                  /* invalid string */
    return;
  } /* if */
  if (c!='\0')
    check_empty(lexptr+1);      /* verify that the rest of the line is whitespace */

  /* create a symbol from the name of the include file; this allows the system
   * to test for multiple inclusions
   */
  strcpy(symname,"_inc_");
  if ((ptr=strrchr(name,DIRSEP_CHAR))!=NULL)
    strlcat(symname,ptr+1,countof(symname));
  else
    strlcat(symname,name,countof(symname));
  /* replace invalid characters by '_' (anything not a digit, character or
   * underscore)
   */
  for (i=0; symname[i]!='\0'; i++)
    if (!alphanum(symname[i]))
      symname[i]='_';
# if defined __WIN32__ || defined _WIN32 || defined _Windows || defined __MSDOS__
    /* on systems with case-insentive filenames, force the symbol for the file
     * to lower case
     */
    strlwr(symname);
# endif
  if (find_symbol(&glbtab,symname,fcurrent,0)==NULL) {
    /* constant is not present, so this file has not been included yet */

    /* Include files between "..." or without quotes are read from the same
     * relative path as the current file, from the active directory, or from
     * a list of "include directories". Include files between <...> are only
     * read from the list of include directories.
     */
    bool result=plungefile(name,(c!='>'),true);
    if (result)
      add_constant(symname,1,sGLOBAL,0);
    else if (!silent)
      error(100,name);            /* cannot read from ... (fatal error) */
  } /* if */
}

/*  readline
 *
 *  Reads in a new line from the input file pointed to by "inpf". readline()
 *  concatenates lines that end with a \ with the next line. If no more data
 *  can be read from the file, readline() attempts to pop off the previous file
 *  from the stack. If that fails too, it sets "freading" to false.
 *
 *  Global references: inpf,pc_curline,inpfname,freading,icomment (modified)
 */
static void readline(unsigned char *line,int append)
{
  int i,cont;
  unsigned char *ptr;
  size_t num;

  if (lexptr==term_expr)
    return;
  num=sLINEMAX;
  if (append) {
    i=(int)strlen((char*)line);
    num-=i;
    line+=i;
  } /* if */
  cont=false;
  do {
    symbol *sym;
    if (inpf==NULL || pc_eofsrc(inpf)) {
      if (cont)
        error(49);        /* invalid line continuation */
      else if (append)
        error(1,"]","-end of file-");
      if (inpf!=NULL && inpf!=inpf_org)
        pc_closesrc(inpf);
      i=POPSTK_I();
      if (i==-1) {        /* All's done; popstk() returns "stack is empty" */
        freading=false;
        *line='\0';
        /* when there is nothing more to read, the #if/#else stack should
         * be empty and we should not be in a comment
         */
        assert(iflevel>=0);
        if (iflevel>0)
          error(1,"#endif","-end of file-");
        else if (icomment!=0)
          error(1,"*/","-end of file-");
        return;
      } /* if */
      pc_curline=i;
      fcurrent=(short)POPSTK_I();
      icomment=(short)POPSTK_I();
      sc_is_utf8=(short)POPSTK_I();
      iflevel=(short)POPSTK_I();
      skiplevel=iflevel;        /* this condition held before including the file */
      assert(!SKIPPING);        /* idem ditto */
      curlibrary=(constvalue *)POPSTK_P();
      free(inpfname);           /* return memory allocated for the include file name */
      inpfname=(char *)POPSTK_P();
      inpf=(FILE *)POPSTK_P();
      if (sc_status==statSKIP)
        error(1,"}","-end of file-"); /* function not finished at EOF */
      insert_dbgfile(inpfname);
      setfiledirect(inpfname);
      assert(sc_status==statBROWSE || strcmp(get_inputfile(fcurrent),inpfname)==0);
      listline=-1;              /* force a #line directive when changing the file */
    } /* if */

    if (!pc_readsrc(inpf,line,num)) {
      *line='\0';     /* delete line */
      cont=false;
    } else {
      /* check whether to erase leading spaces */
      if (cont) {
        unsigned char *ptr2=line;
        while (*ptr2<=' ' && *ptr2!='\0')
          ptr2++;
        if (ptr2!=line)
          memmove(line,ptr2,strlen((char*)ptr2)+1);
      } /* if */
      cont=false;
      /* check whether a full line was read */
      if (strchr((char*)line,'\n')==NULL && !pc_eofsrc(inpf))
        error(75);      /* line too long */
      /* check if the next line must be concatenated to this line */
      if ((ptr=(unsigned char*)strchr((char*)line,'\n'))==NULL)
        ptr=(unsigned char*)strchr((char*)line,'\r');
      if (ptr!=NULL && ptr>line) {
        assert(*(ptr+1)=='\0'); /* '\n' or '\r' should be last in the string */
        while (ptr>line && *ptr<=' ')
          ptr--;        /* skip trailing whitespace */
        if (*ptr=='\\') {
          cont=true;
          /* set '\a' at the position of '\\' to make it possible to check
           * for a line continuation in a single line comment (error 49)
           */
          *ptr++='\a';
          *ptr='\0';    /* erase '\n' (and any trailing whitespace) */
        } /* if */
      } /* if */
      num-=strlen((char*)line);
      line+=strlen((char*)line);
    } /* if */
    pc_curline+=1;
    sym=findconst("__line");
    assert(sym!=NULL);
    sym->addr=pc_curline;
  } while (num>0 && cont);
}

static unsigned char *skip_quotedstring(unsigned char *line)
{
  unsigned char quote=*line++;
  assert(quote=='\'' || quote=='`' || quote=='"');  /* function should be called on whole string */
  if ((quote=='\'' || quote=='`') && *line==quote) {
    /* plain string, ends with doubled single quote */
    line++;
    while ((*line!='\'' || *(line+1)!='\'') && *line!='\0') {
      if (*line==ESCAPE_CHAR && *(line+1)!='\0')
        line++; /* skip escape character (but avoid skipping past '\0') */
      line++;
    }
    if (*line=='\'' && *(line+1)=='\'')
      line+=2;  /* skip final quotes */
  } else {
    while (*line!=quote && *line!='\0') {
      if (*line==ESCAPE_CHAR && *(line+1)!='\0')
        line++; /* skip escape character (but avoid skipping past '\0') */
      line++;
    }
    if (*line==quote)
      line++;   /* skip final quote */
  }
  return line;
}

static int isquotednumber(const unsigned char *line,const unsigned char *head)
{
  int test_hex=0;
  const unsigned char *ptr;

  assert(line!=NULL && head!=NULL);
  assert(*line=='\'');
  /* check the characters following the quote */
  ptr=line+1;
  while (ishex(*ptr)) {
    if (!isdigit(*ptr))
      test_hex=1;
    ptr++;
  }
  if (ptr==line+1 || isalpha(*ptr) || *ptr=='_' || *ptr=='@')
    return 0; /* not a valid quoted number */
  /* check the characters preceding the quote */
  ptr=line-1;
  while (ptr>=head && ishex(*ptr)) {
    if (!isdigit(*ptr))
      test_hex=1;
    ptr--;
  }
  if (ptr==line-1)
    return 0;
  if (test_hex && *ptr!='x' && ptr>=head && *(ptr-1)!='0')
    return 1;
  return (!isalpha(*ptr) && *ptr!='_' && *ptr!='@');
}

/*  stripcom
 *
 *  Replaces all comments from the line by space characters. It updates
 *  a global variable ("icomment") for multiline comments.
 *
 *  This routine also supports the C++ extension for single line comments.
 *  These comments are started with "//" and end at the end of the line.
 *
 *  The function also detects (and manages) "documentation comments". The
 *  global variable "icomment" is set to 2 for documentation comments.
 *
 *  Global references: icomment  (modified)
 */
static void stripcom(unsigned char *line)
{
  unsigned char *head=line;
# if !defined PAWN_LIGHT
#   define COMMENT_LIMIT 100
#   define COMMENT_MARGIN 40   /* length of the longest word */
    char comment[COMMENT_LIMIT+COMMENT_MARGIN];
    int commentidx=0;
    int skipstar=true;
    static int prev_singleline=false;
    int singleline=prev_singleline;

    prev_singleline=false;  /* preset */
# endif

  while (*line){
    if (icomment!=0) {
      if (*line=='*' && *(line+1)=='/') {
#       if !defined PAWN_LIGHT
          if (icomment==2) {
            assert(commentidx<COMMENT_LIMIT+COMMENT_MARGIN);
            comment[commentidx]='\0';
            if (strlen(comment)>0)
              insert_docstring(comment,1);
          } /* if */
#       endif
        icomment=0;     /* comment has ended */
        *line=' ';      /* replace '*' and '/' characters by spaces */
        *(line+1)=' ';
        line+=2;
      } else {
        if (*line=='/' && *(line+1)=='*')
          error(216);   /* nested comment */
#       if !defined PAWN_LIGHT
          /* collect the comment characters in a string */
          if (icomment==2) {
            if (skipstar && (*line!='\0' && *line<=' ' || *line=='*')) {
              /* ignore leading whitespace and '*' characters */
            } else if (commentidx<COMMENT_LIMIT+COMMENT_MARGIN-1) {
              comment[commentidx++]=(char)((*line!='\n') ? *line : ' ');
              if (commentidx>COMMENT_LIMIT && *line!='\0' && *line<=' ') {
                comment[commentidx]='\0';
                insert_docstring(comment,1);
                commentidx=0;
              } /* if */
              skipstar=false;
            } /* if */
          } /* if */
#       endif
        *line=' ';      /* replace comments by spaces */
        line+=1;
      } /* if */
    } else {
      if (*line=='/' && *(line+1)=='*'){
        icomment=1;     /* start comment */
#       if !defined PAWN_LIGHT
          /* there must be two "*" behind the slash and then white space */
          if (*(line+2)=='*' && *(line+3)<=' ') {
            /* if we are not in a function, we must attach the previous block
             * to the global documentation
             */
            if (curfunc==NULL && get_docstring(0)!=NULL)
              sc_attachdocumentation(NULL,false);
            icomment=2; /* documentation comment */
          } /* if */
          commentidx=0;
          skipstar=true;
#       endif
        *line=' ';      /* replace '/' and '*' characters by spaces */
        *(line+1)=' ';
        line+=2;
        if (icomment==2)
          *line++=' ';
      } else if (*line=='/' && *(line+1)=='/'){  /* comment to end of line */
        if (strchr((char*)line,'\a')!=NULL)
          error(49);    /* invalid line continuation */
#       if !defined PAWN_LIGHT
          if (*(line+2)=='/' && *(line+3)<=' ') {
            /* documentation comment */
            char *str=(char*)line+3;
            char *end;
            while (*str<=' ' && *str!='\0')
              str++;    /* skip leading whitespace */
            if ((end=strrchr(str,'\n'))!=NULL)
              *end='\0';/* erase trailing '\n' */
            /* if there is a disjunct block, we may need to attach the previous
             * block to the global documentation
             */
            if (!singleline && curfunc==NULL && get_docstring(0)!=NULL)
              sc_attachdocumentation(NULL,false);
            insert_docstring(str,1);
            prev_singleline=true;
          } /* if */
#       endif
        *line++='\n';   /* put "newline" at first slash */
        *line='\0';     /* put "zero-terminator" at second slash */
      } else if (*line=='"') { /* leave string literals unaltered */
        line=skip_quotedstring(line);
      } else if (*line=='\'') {
        if (isquotednumber(line,head))
          line++;
        else
          line=skip_quotedstring(line);
      } else if (*line=='`' && *(line+1)=='`') {
        line=skip_quotedstring(line);
      } else {
        line+=1;
      }
    } /* if (icomment) */
  } /* while (*line) */
# if !defined PAWN_LIGHT
    if (icomment==2) {
      assert(commentidx<COMMENT_LIMIT+COMMENT_MARGIN);
      comment[commentidx]='\0';
      if (strlen(comment)>0)
        insert_docstring(comment,1);
    } /* if */
#   undef COMMENT_LIMIT
#   undef COMMENT_MARGIN
# endif
}

/*  btoi
 *
 *  Attempts to interpret a numeric symbol as a boolean value. On success
 *  it returns the number of characters processed (so the line pointer can be
 *  adjusted) and the value is stored in "val". Otherwise it returns 0 and
 *  "val" is garbage.
 *
 *  A boolean value must start with "0b"
 */
static int btoi(cell *val,const unsigned char *curptr)
{
  const unsigned char *ptr;

  *val=0;
  ptr=curptr;
  if (*ptr=='0' && *(ptr+1)=='b') {
    unsigned digitsep=UINT_MAX;
    ptr+=2;
    while (*ptr=='0' || *ptr=='1' || *ptr=='\'') {
      if (*ptr=='\'') {
        if (digitsep!=0 && digitsep<INT_MAX)
          return 0;       /* invalid numeric format */
        digitsep=8;
      } else {
        *val=(*val<<1) | (*ptr-'0');
        digitsep--;
      } /* if */
      ptr++;
    } /* while */
    if (digitsep==8)
      ptr--;
    else if (digitsep!=0 && digitsep<INT_MAX)
      return 0;           /* invalid numeric format */
  } else {
    return 0;
  } /* if */
  if (alphanum(*ptr))   /* number must be delimited by non-alphanumeric char */
    return 0;
  else
    return (int)(ptr-curptr);
}

/*  dtoi
 *
 *  Attempts to interpret a numeric symbol as a decimal value. On success
 *  it returns the number of characters processed and the value is stored in
 *  "val". Otherwise it returns 0 and "val" is garbage.
 */
static int dtoi(cell *val,const unsigned char *curptr)
{
  const unsigned char *ptr;
  unsigned digitsep=UINT_MAX; /* thousands separator */

  *val=0;
  ptr=curptr;
  if (!isdigit(*ptr))   /* should start with digit */
    return 0;
  while (isdigit(*ptr) || *ptr=='\'') {
    if (*ptr=='\'') {
      if (digitsep!=0 && digitsep<INT_MAX)
        return 0;       /* invalid numeric format */
      digitsep=3;
    } else {
      *val=(*val*10)+(*ptr-'0');
      digitsep--;
    } /* if */
    ptr++;
  } /* while */
  if (digitsep==3)
    ptr--;
  else if (digitsep!=0 && digitsep<INT_MAX)
    return 0;           /* invalid numeric format */
  if (alphanum(*ptr))   /* number must be delimited by non-alphanumerical */
    return 0;
  if (*ptr=='.' && isdigit(*(ptr+1)))
    return 0;           /* but a fractional part must not be present */
  return (int)(ptr-curptr);
}

/*  htoi
 *
 *  Attempts to interpret a numeric symbol as a hexadecimal value. On
 *  success it returns the number of characters processed and the value is
 *  stored in "val". Otherwise it return 0 and "val" is garbage.
 */
static int htoi(cell *val,const unsigned char *curptr)
{
  const unsigned char *ptr;

  *val=0;
  ptr=curptr;
  if (!isdigit(*ptr))   /* should start with digit */
    return 0;
  if (*ptr=='0' && *(ptr+1)=='x') {     /* C style hexadecimal notation */
    unsigned digitsep=UINT_MAX;
    ptr+=2;
    while (ishex(*ptr) || *ptr=='\'') {
      if (*ptr=='\'') {
        if (digitsep!=0 && digitsep<INT_MAX)
          return 0;       /* invalid numeric format */
        digitsep=4;
      } else {
        assert(ishex(*ptr));
        *val= *val<<4;
        if (isdigit(*ptr))
          *val+= (*ptr-'0');
        else
          *val+= (tolower(*ptr)-'a'+10);
        digitsep--;
      } /* if */
      ptr++;
    } /* while */
    if (digitsep==4)
      ptr--;
    else if (digitsep!=0 && digitsep<INT_MAX)
      return 0;           /* invalid numeric format */
  } else {
    return 0;
  } /* if */
  if (alphanum(*ptr))     /* number must be delimited by non-alphanumerical */
    return 0;
  else
    return (int)(ptr-curptr);
}

/*  ftoi
 *
 *  Attempts to interpret a numeric symbol as a rational number, either as
 *  IEEE 754 single/double precision floating point or as a fixed point integer.
 *  On success it returns the number of characters processed and the value is
 *  stored in "val". Otherwise it returns 0 and "val" is unchanged.
 *
 *  Pawn has stricter definition for rational numbers than most:
 *  o  the value must start with a digit; ".5" is not a valid number, you
 *     should write "0.5"
 *  o  a period must appear in the value, even if an exponent is given; "2e3"
 *     is not a valid number, you should write "2.0e3"
 *  o  at least one digit must follow the period; "6." is not a valid number,
 *     you should write "6.0"
 */
static int ftoi(cell *val,const unsigned char *curptr)
{
  const unsigned char *ptr;
  double fnum,ffrac,fmult;
  unsigned long dnum,dbase;
  int i, ignore;
  unsigned digitsep=UINT_MAX; /* thousands separator */

  if (!isdigit(*curptr))      /* should start with digit */
    return 0;
  assert(rational_digits>=0 && rational_digits<9);
  for (i=0,dbase=1; i<rational_digits; i++)
    dbase*=10;
  fnum=0.0;
  dnum=0L;
  ptr=curptr;
  while (isdigit(*ptr) || *ptr=='\'') {
    if (*ptr=='\'') {
      if (digitsep!=0 && digitsep<INT_MAX)
        return 0;       /* invalid numeric format */
      digitsep=3;
    } else {
      fnum=(fnum*10.0)+(*ptr-'0');
      dnum=(dnum*10L)+(*ptr-'0')*dbase;
      digitsep--;
    } /* if */
    ptr++;
  } /* while */
  if (digitsep==3)
    ptr--;
  else if (digitsep!=0 && digitsep<INT_MAX)
    return 0;           /* invalid numeric format */
  if (*ptr!='.')
    return 0;           /* there must be a period */
  ptr++;
  if (!isdigit(*ptr))   /* there must be at least one digit after the dot */
    return 0;
  ffrac=0.0;
  fmult=1.0;
  ignore=false;
  while (isdigit(*ptr)) {
    ffrac=(ffrac*10.0)+(*ptr-'0');
    fmult=fmult/10.0;
    dbase /= 10L;
    dnum += (*ptr-'0')*dbase;
    if (dbase==0L && sc_rationaltag && rational_digits>0 && !ignore) {
      error(222);     /* number of digits exceeds rational number precision */
      ignore=true;
    } /* if */
    ptr++;
  } /* while */
  fnum += ffrac*fmult;  /* form the number so far */
  if (*ptr=='e') {      /* optional fractional part */
    int exp,sign;
    ptr++;
    if (*ptr=='-') {
      sign=-1;
      ptr++;
    } else {
      sign=1;
    } /* if */
    if (!isdigit(*ptr)) /* 'e' should be followed by a digit */
      return 0;
    exp=0;
    while (isdigit(*ptr)) {
      exp=(exp*10)+(*ptr-'0');
      ptr++;
    } /* while */
    fmult=pow(10,exp*sign);
    fnum *= fmult;
    dnum *= (unsigned long)(fmult+0.5);
  } /* if */

  /* decide how to store the number */
  if (sc_rationaltag==0) {
    error(70);          /* rational number support was not enabled */
    *val=0;
  } else if (rational_digits==0) {
    /* floating point */
    assert(pc_cellsize==4 || pc_cellsize==8);
    if (pc_cellsize==4) {
      /* although the selected cell size is 32-bits, the compiler actually
         always uses a 64-bit cell type internally, so copy through an
         explicitly 32-bit type */
      float fnum32=(float)fnum; /* first convert to 4-byte float */
      uint32_t inum32;
      memcpy(&inum32,&fnum32,sizeof inum32);
      *val=(cell)inum32;
#     if !defined NDEBUG
        /* I assume that the C/C++ compiler stores "float" values in IEEE 754
         * format (as mandated in the ANSI standard). Test this assumption
         * anyway.
         * Note: problems have been reported with GCC 3.2.x, version 3.3.x works.
         */
        { float test1 = 0.0, test2 = 50.0, test3 = -50.0;
          uint32_t bit = 1;
          /* test 0.0 == all bits 0 */
          assert(*(uint32_t*)&test1==0x00000000L);
          /* test sign & magnitude format */
          assert(((*(uint32_t*)&test2) ^ (*(uint32_t*)&test3)) == (bit << (8*pc_cellsize-1)));
          /* test a known value */
          assert(*(uint32_t*)&test2==0x42480000L);
        }
#     endif
    } else {
      uint64_t inum64;
      assert(sizeof(inum64)==sizeof(fnum));
      memcpy(&inum64,&fnum,sizeof inum64);
      *val=(cell)inum64;
#     if !defined NDEBUG
        /* I assume that the C/C++ compiler stores "double" values in IEEE 754
         * format (as mandated in the ANSI standard).
         */
        { double test1 = 0.0, test2 = 50.0, test3 = -50.0;
          uint64_t bit = 1;
          /* test 0.0 == all bits 0 */
          assert(*(uint64_t*)&test1==0x00000000L);
          /* test sign & magnitude format */
          assert(((*(uint64_t*)&test2) ^ (*(uint64_t*)&test3)) == (bit << (8*pc_cellsize-1)));
        }
#     endif
    } /* if */
  } else {
    /* fixed point */
    *val=(cell)dnum;
  } /* if */

  return (int)(ptr-curptr);
}

/*  number
 *
 *  Reads in a number (binary, decimal or hexadecimal). It returns the number
 *  of characters processed or 0 if the symbol couldn't be interpreted as a
 *  number (in this case the argument "val" remains unchanged). This routine
 *  relies on the 'early dropout' implementation of the logical or (||)
 *  operator.
 *
 *  Note: the routine doesn't check for a sign (+ or -). The - is checked
 *        for at "hier2()" (in fact, it is viewed as an operator, not as a
 *        sign) and the + is invalid (as in K&R C, and unlike ANSI C).
 */
SC_FUNC int number(cell *val,const unsigned char *curptr)
{
  int i;
  cell value;

  if ((i=btoi(&value,curptr))!=0      /* binary? */
      || (i=htoi(&value,curptr))!=0   /* hexadecimal? */
      || (i=dtoi(&value,curptr))!=0)  /* decimal? */
  {
    *val=value;
    return i;
  } else {
    return 0;                         /* else not a number */
  } /* if */
}

static void chrcat(char *str,char chr)
{
  str=strchr(str,'\0');
  *str++=chr;
  *str='\0';
}

/** preproc_expr() parses a constant expression, and clears any code or
 *  literal constants that the parser created in the process. Pre-processing
 *  directives should not leave traces in the generated code.
 *
 *  \param val          [out] The value of the constant expression (on success,
 *                      set to 0 on error).
 *  \param tag          [out] The tag of the constant expression (set to 0 on
 *                      error).
 *  \param clear_lex    Whether any pushed tokens must be cleared before the
 *                      return of this function. This should be `true` when only
 *                      a single expression is expected on the directive, but
 *                      may be set to `false` when a list of expressions may be
 *                      present (in which case the caller must clear the lex
 *                      state after finishing with the last expression).
 *
 *  \return `true` on success, `false` on failure.
 */
static bool preproc_expr(cell *val,int *tag,bool clear_lex)
{
  /* Disable staging; it should be disabled already because
   * expressions may not be cut off half-way between conditional
   * compilations. Reset the staging index, but keep the code
   * index.
   */
  unsigned index;
  cell code_index;
  if (stgget(&index,&code_index)) {
    error(57);                          /* unfinished expression */
    stgdel(0,code_index);
    stgset(false);
  } /* if */
  /* save the position in the literal queue too */
  unsigned cur_lit=litidx;
  assert(lexptr!=NULL && srcline!=NULL);
  assert(lexptr>=srcline && (lexptr-srcline)<(int)strlen((char*)srcline));   /* lexptr must point inside the string */
# if !defined NO_DEFINE
    /* preprocess the string */
    substallpatterns(srcline,sLINEMAX);
    assert(lexptr>=srcline && (lexptr-srcline)<(int)strlen((char*)srcline)); /* lexptr must STILL point inside the string */
# endif
  /* append a special symbol to the string, so the expression
   * analyzer won't try to read a next line when it encounters
   * an end-of-line
   */
  assert(strlen((char*)srcline)<sLINEMAX);
  unsigned char *term=srcline+strlen((char*)srcline);
  assert(term!=NULL);
  chrcat((char*)srcline,PREPROC_TERM);      /* the "DEL" code (see sc.h) */
  bool result=constexpression(val,tag,NULL);/* get value (or 0 on error) */
  *term='\0';                               /* erase the token (if still present) */
  if (clear_lex)
    lexclr(false);                          /* clear any "pushed" tokens */
  litidx=cur_lit;                           /* reset literal pool */
  return result;
}

/** pp_getstring() parses a quoted string from the input line and stores it in a
 *  destination buffer. This routine is for the preprocessor (directives) and it
 *  does not handle escape characters. The input string can be quoted or
 *  unquoted.
 *
 *  \param dest         [out, optional] The output buffer. This parameter may be
 *                      `NULL`, in which case no output is stored (but the
 *                      string is still parsed).
 *  \param max          The maximum size of the `dest` buffer. This should be 0
 *                      if `dest` is `NULL`.
 *  \param line         [in] The buffer to read the string from.
 *
 *  \return A pointer to just behind the closing quote or to the other character
 *          that caused the input to be ended.
 *
 *  \note If the string is unquoted, parsing stops at the first white-space
 *        character.
 */
static const unsigned char *pp_getstring(unsigned char *dest,size_t max,const unsigned char *line)
{
  assert(line!=NULL);
  assert((dest!=NULL && max>0) || (dest==NULL && max==0));
  if (dest)
    *dest='\0';
  while (*line<=' ' && *line!='\0')
    line++;             /* skip whitespace */
  size_t len=0;
  if (*line=='"') {
    line++;             /* skip " */
    while (*line!='"' && *line!='\0') {
      if (dest && len<max-1)
        dest[len++]=*line;
      line++;
    } /* while */
    if (*line=='"')
      line++;           /* skip closing " */
    else
      error(37);        /* invalid string */
  } else {
    while (*line>' ') {
      if (dest && len<max-1)
        dest[len++]=*line;
      line++;
    } /* while */
  } /* if */
  if (dest) {
    assert(len<max);
    dest[len]='\0';
  } /* if */
  return line;
}

static void pragma_warning_loop(unsigned char flag)
{
  do {
    cell val;
    if (!preproc_expr(&val,NULL,false))
      break;
    pc_enablewarning((int)val,flag);
  } while (matchtoken(','));
  lexclr(false);                    /* clear any "pushed" tokens */
}

enum {
  CMD_NONE,
  CMD_TERM,
  CMD_EMPTYLINE,
  CMD_CONDFALSE,
  CMD_INCLUDE,
  CMD_DEFINE,
  CMD_IF,
  CMD_DIRECTIVE,
};

/*  command
 *
 *  Recognizes the compiler directives. The function returns:
 *     CMD_NONE         the line must be processed
 *     CMD_TERM         a pending expression must be completed before processing further lines
 *     Other value: the line must be skipped, because:
 *     CMD_CONDFALSE    false "#if.." code
 *     CMD_EMPTYLINE    line is empty
 *     CMD_INCLUDE      the line contains a #include directive
 *     CMD_DEFINE       the line contains a #define directive
 *     CMD_IF           the line contains a #if/#else/#endif directive
 *     CMD_DIRECTIVE    the line contains some other compiler directive
 *
 *  Global variables: iflevel, ifstack (modified)
 *                    lexptr        (modified)
 *                    fcurrent      (read-only)
 *                    pc_curline    (read-only)
 *                    pc_scopelevel (read-only)
 */
static int command(void)
{
  int tok,ret;
  unsigned index;
  cell code_index;

  while (*lexptr<=' ' && *lexptr!='\0')
    lexptr+=1;
  if (*lexptr=='\0')
    return CMD_EMPTYLINE;       /* empty line */
  if (*lexptr!='#')
    return SKIPPING ? CMD_CONDFALSE : CMD_NONE; /* it is not a compiler directive */
  /* compiler directive found */
  indent_nowarn=true;           /* allow loose indentation" */
  lexclr(false);                /* clear any "pushed" tokens */
  /* on a pending expression, force to return a silent ';' token and force to
   * re-read the line
   */
  if (stgget(&index,&code_index)) {
    lexptr=term_expr;
    return CMD_TERM;
  } /* if */
  cell lexval;
  char *lexstr;
  tok=lex(&lexval,&lexstr);
  ret=SKIPPING ? CMD_CONDFALSE : CMD_DIRECTIVE;  /* preset 'ret' to CMD_DIRECTIVE (most common case) */
  switch (tok) {
  case tpIF:                    /* conditional compilation */
  case tpIFDEF:
  case tpIFNDEF:
    ret=CMD_IF;
    assert(iflevel>=0);
    if (iflevel>=sCOMP_STACK)
      error(102,"Conditional compilation stack"); /* table overflow */
    iflevel++;
    if (SKIPPING)
      break;                    /* break out of switch */
    skiplevel=iflevel;
    if (tok==tpIF) {
      cell val;
      preproc_expr(&val,NULL,true);     /* get value (or 0 on error) */
      ifstack[iflevel-1]=(char)(val ? PARSEMODE : SKIPMODE);
    } else {
      int symtok=lex(&lexval,&lexstr);
      bool found=false;
      if (symtok==tSYMBOL) {
        const symbol *sym=findloc(lexstr);
        if (sym==NULL)
          sym=findglb(lexstr,sGLOBAL);
        if (sym!=NULL
            && (((sym->usage & uDEFINE)==0 && sym->ident!=iFUNCTN)
                || (sym->ident==iFUNCTN && (sym->usage & uPROTOTYPED)==0)))
          sym=NULL;                 /* symbol is in the table, but not as "defined" or "prototyped" */
        if (sym!=NULL || find_subst(lexstr,(int)strlen(lexstr))!=NULL)
          found=true;
        if (!found)
          insert_undefsymbol(lexstr,fcurrent,pc_curline); /* if the symbol is not defined, add it to the "undefined symbols" list */
        if (tok==tpIFNDEF)
          found=!found;
      } else {
        error(76);  /* syntax error */
      }
      ifstack[iflevel-1]=(char)(found ? PARSEMODE : SKIPMODE);
    }
    check_empty(lexptr);
    break;
  case tpELSE:
  case tpELSEIF:
    ret=CMD_IF;
    assert(iflevel>=0);
    if (iflevel==0) {
      error(26);                /* no matching #if */
      errorset(sRESET);
    } else {
      /* check for earlier #else */
      if ((ifstack[iflevel-1] & HANDLED_ELSE)==HANDLED_ELSE) {
        if (tok==tpELSEIF)
          error(61);            /* #elseif directive may not follow an #else */
        else
          error(60);            /* multiple #else directives between #if ... #endif */
        errorset(sRESET);
      } else {
        assert(iflevel>0);
        /* if there has been a "parse mode" on this level, set "skip mode",
         * otherwise, clear "skip mode"
         */
        if ((ifstack[iflevel-1] & PARSEMODE)==PARSEMODE) {
          /* there has been a parse mode already on this level, so skip the rest */
          ifstack[iflevel-1] |= (char)SKIPMODE;
          /* if we were already skipping this section, allow expressions with
           * undefined symbols; otherwise check the expression to catch errors
           */
          if (tok==tpELSEIF) {
            if (skiplevel==iflevel) {
              cell val;
              preproc_expr(&val,NULL,true);  /* get, but ignore the expression */
            } else {
              lexptr=(const unsigned char*)strchr((const char*)lexptr,'\0');
            } /* if */
          } /* if */
        } else {
          /* previous conditions were all false */
          if (tok==tpELSEIF) {
            /* if we were already skipping this section, allow expressions with
             * undefined symbols; otherwise check the expression to catch errors
             */
            cell val;
            if (skiplevel==iflevel) {
              preproc_expr(&val,NULL,true);  /* get value (or 0 on error) */
            } else {
              lexptr=(const unsigned char*)strchr((const char *)lexptr,'\0');
              val=0;
            } /* if */
            ifstack[iflevel-1]=(char)(val ? PARSEMODE : SKIPMODE);
          } else {
            /* a simple #else, clear skip mode */
            ifstack[iflevel-1] &= (char)~SKIPMODE;
          } /* if */
        } /* if */
      } /* if */
    } /* if */
    check_empty(lexptr);
    break;
  case tpENDIF:
    ret=CMD_IF;
    if (iflevel==0){
      error(26);                    /* no matching "#if" */
      errorset(sRESET);
    } else {
      iflevel--;
      if (iflevel<skiplevel)
        skiplevel=iflevel;
    } /* if */
    check_empty(lexptr);
    break;
  case tpINCLUDE:                   /* #include directive */
  case tpTRYINCLUDE:
    ret=CMD_INCLUDE;
    if (!SKIPPING)
      doinclude(tok==tpTRYINCLUDE);
    break;
  case tpFILE:
    if (!SKIPPING) {
      char pathname[_MAX_PATH];
      lexptr=pp_getstring((unsigned char*)pathname,countof(pathname),lexptr);
      if (strlen(pathname)>0) {
        free(inpfname);
        inpfname=duplicatestring(pathname);
        if (inpfname==NULL)
          error(103);               /* insufficient memory */
        /* adjust fcurrent too, after storing the filename in the file table (if needed) */
        fcurrent=find_inputfile(pathname);
        if (fcurrent<0) {
          insert_inputfile(pathname);
          fnumber++;
          fcurrent=fnumber;
        } /* if */
        insert_dbgfile(pathname);
      } /* if */
    } else {
      lexptr=pp_getstring(NULL,0,lexptr);
    } /* if */
    check_empty(lexptr);
    break;
  case tpLINE:
    if (!SKIPPING) {
      if (lex(&lexval,&lexstr)!=tNUMBER)
        error(8);                   /* invalid/non-constant expression */
      pc_curline=(int)lexval;
    } /* if */
    check_empty(lexptr);
    break;
  case tpASSERT:
    if (!SKIPPING && (sc_debug & sCHKBOUNDS)!=0) {
      const char *expr;
      for (expr=(const char*)lexptr; *expr<=' ' && *expr!='\0'; expr++)
        /* nothing */;              /* save start of expression */
      cell val;
      preproc_expr(&val,NULL,true); /* get constant expression (or 0 on error) */
      if (!val)
        error(110,expr);            /* assertion failed */
      check_empty(lexptr);
    } /* if */
    break;
  case tpPRAGMA:
    if (!SKIPPING) {
      int ok= (lex(&lexval,&lexstr)==tSYMBOL);
      if (ok) {
        if (strcmp(lexstr,"align")==0) {
          pc_pendingflags|=flgALIGNDATA;
        } else if (strcmp(lexstr,"amxlimit")==0) {
          preproc_expr(&pc_amxlimit,NULL,true);
        } else if (strcmp(lexstr,"amxram")==0) {
          preproc_expr(&pc_amxram,NULL,true);
#if !defined PAWN_NO_CODEPAGE
        } else if (strcmp(lexstr,"codepage")==0) {
          char name[sNAMEMAX+1];
          while (*lexptr<=' ' && *lexptr!='\0')
            lexptr++;
          if (*lexptr=='"') {
            lexptr=pp_getstring((unsigned char*)name,countof(name),lexptr);
          } else {
            int i;
            for (i=0; i<(int)countof(name) && alphanum(*lexptr); i++,lexptr++)
              name[i]=*lexptr;
            name[i]='\0';
          } /* if */
          if (!cp_set(name))
            error(108);         /* codepage mapping file not found */
#endif
        } else if (strcmp(lexstr,"deprecated")==0) {
          while (*lexptr<=' ' && *lexptr!='\0')
            lexptr++;
          pc_deprecate=(char*)malloc(strlen((char*)lexptr)+1);
          if (pc_deprecate!=NULL)
            strcpy(pc_deprecate,(char*)lexptr);
          lexptr=(unsigned char*)strchr((char*)lexptr,'\0'); /* skip to end (ignore "extra characters on line") */
        } else if (strcmp(lexstr,"dynamic")==0) {
          preproc_expr(&pc_stksize,NULL,true);
        } else if (strcmp(lexstr,"library")==0) {
          char name[sNAMEMAX+1];
          while (*lexptr<=' ' && *lexptr!='\0')
            lexptr++;
          if (*lexptr=='"') {
            lexptr=pp_getstring((unsigned char*)name,countof(name),lexptr);
          } else {
            int i;
            for (i=0; i<(int)countof(name) && (alphanum(*lexptr) || *lexptr=='-'); i++,lexptr++)
              name[i]=*lexptr;
            name[i]='\0';
          } /* if */
          if (strlen(name)==0) {
            curlibrary=NULL;
          } else if (strcmp(name,"-")==0) {
            pc_addlibtable=false;
          } else {
            /* select the library, adding it if it is not in the table yet */
            curlibrary=find_constval(&libname_tab,name,-1);
            if (curlibrary==NULL)
              curlibrary=append_constval(&libname_tab,name,0,0);
          } /* if */
        } else if (strcmp(lexstr,"opcodeset")==0) {
          cell val;
          preproc_expr(&val,NULL,true);
          if (val>0 && val<pc_optimize)
            pc_optimize=(int)val;
        } else if (strcmp(lexstr, "overlaysize") == 0) {
          symbol *sym;
          cell val;
          preproc_expr(&val,NULL,true);
          pc_overlays=(int)val; /* switch overlay code generation on/off,
                                 * also set the size of the overlay buffer */
          sym=findconst("overlaysize");
          assert(sym!=NULL);
          sym->addr=val;
        } else if (strcmp(lexstr,"rational")==0) {
          char name[sNAMEMAX+1];
          cell digits=0;
          int i;
          /* first gather all information, start with the tag name */
          while (*lexptr<=' ' && *lexptr!='\0')
            lexptr++;
          for (i=0; i<(int)countof(name) && alphanum(*lexptr); i++,lexptr++)
            name[i]=*lexptr;
          name[i]='\0';
          /* then the precision (for fixed point arithmetic) */
          while (*lexptr<=' ' && *lexptr!='\0')
            lexptr++;
          if (*lexptr=='(') {
            preproc_expr(&digits,NULL,true);
            if (digits<=0 || digits>9) {
              error(68);        /* invalid rational number precision */
              digits=0;
            } /* if */
            if (*lexptr==')')
              lexptr++;
          } /* if */
          if (pc_cellsize<4) {
            error(68);          /* rational numbers not supported on 16-bit cells */
            break;
          } /* if */
          /* add the tag (make it public) and check the values */
          i=pc_addtag(name);
          exporttag(i);
          if (sc_rationaltag==0 || (sc_rationaltag==i && rational_digits==(int)digits)) {
            sc_rationaltag=i;
            rational_digits=(int)digits;
          } else {
            error(69);          /* rational number format already set, can only be set once */
          } /* if */
        } else if (strcmp(lexstr,"recursive")==0) {
          if (curfunc!=NULL)
            curfunc->flags|=flgRECURSIVE;
          else
            pc_pendingflags|=flgRECURSIVE;
        } else if (strcmp(lexstr,"tabsize")==0) {
          cell val;
          preproc_expr(&val,NULL,true);
          if (2<=val && val<=8)
            pc_tabsize=pc_matchedtabsize=(int)val;
        } else if (strcmp(lexstr,"unused")==0) {
          char name[sNAMEMAX+1];
          int i,comma;
          do {
            /* get the name, store it together with the current line number and
               scope */
            while (*lexptr<=' ' && *lexptr!='\0')
              lexptr++;
            for (i=0; i<(int)countof(name) && alphanum(*lexptr); i++,lexptr++)
              name[i]=*lexptr;
            name[i]='\0';
            /* if a variable already exists, copy its scope */
            const symbol *sym= (pc_blocklevel==0) ? findglb(name,sGLOBAL) : findloc(name);
            short blocklevel= (sym!=NULL) ? sym->block : pc_blocklevel;
            add_unusedvar(name,fcurrent,pc_curline,blocklevel);
            /* see if a comma follows the name */
            while (*lexptr<=' ' && *lexptr!='\0')
              lexptr++;
            comma= (*lexptr==',');
            if (comma)
              lexptr++;
          } while (comma);
        } else if (strcmp(lexstr,"warning")==0) {
          ok= (lex(&lexval,&lexstr)==tSYMBOL);
          if (ok) {
            if (strcmp(lexstr,"push")==0)
              pushwarnings();
            else if (strcmp(lexstr,"pop")==0)
              popwarnings();
            else if (strcmp(lexstr,"enable")==0)
              pragma_warning_loop(wENABLE);
            else if (strcmp(lexstr,"disable")==0)
              pragma_warning_loop(wDISABLE);
            else
              ok=false;
          }
        } else {
          ok=false;
        } /* if */
      } /* if */
      if (!ok)
        error(207);             /* unknown #pragma */
      check_empty(lexptr);
    } /* if */
    break;
  case tpENDINPUT:
    if (!SKIPPING) {
      check_empty(lexptr);
      assert(inpf!=NULL);
      if (inpf!=inpf_org)
        pc_closesrc(inpf);
      inpf=NULL;
    } /* if */
    break;
#if !defined NO_DEFINE
  case tpDEFINE: {
    ret=CMD_DEFINE;
    if (!SKIPPING) {
      char *pattern,*substitution;
      const unsigned char *start,*end;
      int count,prefixlen,matchbracket,nest;
      const stringpair *def;
      /* find the pattern to match */
      while (*lexptr<=' ' && *lexptr!='\0')
        lexptr++;
      start=lexptr;       /* save starting point of the match pattern */
      count=0;
      while (*lexptr>' ' && *lexptr!='[') {
        litchar(&lexptr,0); /* litchar() advances "lexptr" and handles escape characters */
        count++;
      } /* while */
      end=lexptr;
      /* check pattern to match */
      if (!alpha(*start)) {
        error(74);      /* pattern must start with an alphabetic character */
        break;
      } /* if */
      /* store matched pattern */
      pattern=(char*)malloc(count+1);
      if (pattern==NULL)
        error(103);     /* insufficient memory */
      lexptr=start;
      count=0;
      while (lexptr!=end) {
        assert(lexptr<end);
        assert(*lexptr!='\0');
        pattern[count++]=(char)litchar(&lexptr,0);
      } /* while */
      pattern[count]='\0';
      /* special case, erase trailing variable, because it could match anything */
      if (count>=2 && isdigit(pattern[count-1]) && pattern[count-2]=='%')
        pattern[count-2]='\0';
      /* find substitution string */
      while (*lexptr<=' ' && *lexptr!='\0')
        lexptr++;
      matchbracket=(*lexptr=='[');
      if (matchbracket)
        lexptr++;         /* skip '[' */
      nest=0;
      start=lexptr;       /* save starting point of the match pattern */
      count=0;
      end=NULL;
      if (matchbracket) {
        /* allow the substitution text to be split over multiple lines */
        while (*lexptr!=']' || nest>0) {
          if (*lexptr=='\0') {
            readline(srcline,true);
            stripcom(srcline);
            if (*lexptr=='\0')
              break;    /* no next line */
            if (*lexptr==']')
              continue;
          } /* if */
          if (*lexptr=='[')
            nest++;
          else if (*lexptr==']')
            nest--;
          count++;
          lexptr++;
        } /* while */
        end=lexptr;
      } else {
        /* the complete definition should appear on a single (logical) line */
        while (*lexptr!='\0') {
          /* keep position of the start of trailing whitespace */
          if (*lexptr<=' ') {
            if (end==NULL)
              end=lexptr;
          } else {
            end=NULL;
          } /* if */
          count++;
          lexptr++;
        } /* while */
        if (end==NULL)
          end=lexptr;
      } /* if */
      /* store matched substitution */
      substitution=(char*)malloc(count+1);  /* +1 for '\0' */
      if (substitution==NULL)
        error(103);             /* insufficient memory */
      lexptr=start;
      count=0;
      while (lexptr!=end) {
        assert(lexptr<end);
        assert(*lexptr!='\0');
        substitution[count++]=*lexptr++;
      } /* while */
      substitution[count]='\0';
      /* check whether the definition already exists */
      for (prefixlen=0,start=(unsigned char*)pattern; alphanum(*start); prefixlen++,start++)
        /* nothing */;
      assert(prefixlen>0);
      if ((def=find_subst(pattern,prefixlen))!=NULL) {
        if (strcmp(def->first,pattern)!=0 || strcmp(def->second,substitution)!=0)
          error(201,pattern);   /* redefinition of macro (non-identical) */
        delete_subst(pattern,prefixlen);
      } /* if */
      /* add the pattern/substitution pair to the list */
      assert(strlen(pattern)>0);
      insert_subst(pattern,substitution,prefixlen);
      free(pattern);
      free(substitution);
      /* check rest of the line (should be empty) */
      if (matchbracket) {
        if (*lexptr!=']')
          error(1,"]","-end of line-");
        else
          check_empty(lexptr+1);  /* skip ']' */
      } /* if */
    } /* if */
    break;
  } /* case */
  case tpUNDEF:
    if (!SKIPPING) {
      if (lex(&lexval,&lexstr)==tSYMBOL) {
        ret=delete_subst(lexstr,(int)strlen(lexstr));
        if (!ret) {
          /* also undefine normal constants */
          symbol *sym=findconst(lexstr);
          if (sym!=NULL) {
            delete_symbol(&glbtab,sym);
            ret=true;
          } /* if */
        } /* if */
        if (!ret)
          error(17,lexstr);          /* undefined symbol */
      } else {
        error_symbolname(20,lexstr); /* invalid symbol name */
      } /* if */
      check_empty(lexptr);
    } /* if */
    break;
#endif
  case tpERROR:
  case tpWARNING:
    while (*lexptr<=' ' && *lexptr!='\0')
      lexptr++;
    if (!SKIPPING) {
      int errcode=(tok==tpERROR) ? 111 : 220;
      error(errcode,lexptr);  /* user error or user warning */
    } /* if */
    break;
  default:
    error(31);          /* unknown compiler directive */
    ret=SKIPPING ? CMD_CONDFALSE : CMD_NONE;  /* process as normal line */
  } /* switch */
  return ret;
}

#if !defined NO_DEFINE
static int is_startstring(const unsigned char *string)
{
  if (*string=='"' || *string=='\'')
    return true;                        /* "..." */

  if (*string==ESCAPE_CHAR) {
    string++;
    if (*string=='\"' || *string=='\'')
      return true;                      /* \"..." */
  } /* if */

  return false;
}

static const unsigned char *skipstring(const unsigned char *string)
{
  char endquote;
  int flags=0;

  if (*string==ESCAPE_CHAR) {
    flags=RAWMODE;
    string++;
  } /* while */

  endquote=*string;
  assert(endquote=='"' || endquote=='\'');
  string++;             /* skip open quote */
  while (*string!=endquote && *string!='\0')
    litchar(&string,flags);
  return string;
}

static const unsigned char *skippgroup(const unsigned char *string)
{
  int nest=0;
  char open=*string;
  char close;

  switch (open) {
  case '(':
    close=')';
    break;
  case '{':
    close='}';
    break;
  case '[':
    close=']';
    break;
  case '<':
    close='>';
    break;
  default:
    assert(0);
    close='\0';         /* only to avoid a compiler warning */
  }/* switch */

  string++;
  while (*string!=close || nest>0) {
    if (*string==open)
      nest++;
    else if (*string==close)
      nest--;
    else if (is_startstring(string))
      string=skipstring(string);
    if (*string=='\0')
      break;
    string++;
  } /* while */
  return string;
}

SC_FUNC char *strdel(char *str,size_t len)
{
  size_t length=strlen(str);
  if (len>length)
    len=length;
  memmove(str, str+len, length-len+1);  /* include EOS byte */
  return str;
}

SC_FUNC char *strins(char *dest,const char *src,size_t srclen)
{
  size_t destlen=strlen(dest);
  assert(srclen<=strlen(src));
  memmove(dest+srclen, dest, destlen+1);/* include EOS byte */
  memcpy(dest, src, srclen);
  return dest;
}

static bool substpattern(unsigned char *line,size_t buffersize,char *pattern,char *substitution)
{
  const unsigned char *p,*s,*e;
  int arg;

  unsigned char *args[10];
  memset(args,0,sizeof args);

  /* check the length of the prefix */
  int prefixlen;
  for (prefixlen=0,s=(unsigned char*)pattern; alphanum(*s); prefixlen++,s++)
    /* nothing */;
  assert(prefixlen>0);
  assert(strncmp((char*)line,pattern,prefixlen)==0);

  /* pattern prefix matches; match the rest of the pattern, gather
   * the parameters
   */
  s=line+prefixlen;
  p=(unsigned char*)pattern+prefixlen;
  bool match=true;      /* so far, pattern matches */
  while (match && *s!='\0' && *p!='\0') {
    if (*p=='%') {
      p++;              /* skip '%' */
      if (isdigit(*p)) {
        arg=*p-'0';
        assert(arg>=0 && arg<=9);
        p++;          /* skip parameter id */
        assert(*p!='\0');
        /* match the source string up to the character after the digit
         * (skipping strings in the process
         */
        e=s;
        while (*e!=*p && *e!='\0' && *e!='\n') {
          if (is_startstring(e))              /* skip strings */
            e=skipstring(e);
          else if (strchr("({[",*e)!=NULL)    /* skip parenthized groups */
            e=skippgroup(e);
          if (*e!='\0')
            e++;      /* skip non-alphapetic character (or closing quote of
                       * a string, or the closing paranthese of a group) */
        } /* while */
        /* store the parameter (overrule any earlier) */
        if (args[arg]!=NULL)
          free(args[arg]);
        size_t len=(int)(e-s);
        args[arg]=(unsigned char*)malloc((len+1)*sizeof(unsigned char));
        if (args[arg]==NULL)
          error(103); /* insufficient memory */
        strlcpy((char*)args[arg],(char*)s,len+1);
        /* character behind the pattern was matched too */
        if (*e==*p) {
          s=e+1;
        } else if (*e=='\n' && *p==';' && *(p+1)=='\0') {
          s=e;    /* allow a trailing ; in the pattern match to end of line */
        } else {
          assert(*e=='\0' || *e=='\n');
          match=false;
          s=e;
        } /* if */
        p++;
      } else {
        match=false;
      } /* if */
    } else if (*p==';' && *(p+1)=='\0') {
      /* source may be ';' or end of the line */
      while (*s<=' ' && *s!='\0')
        s++;          /* skip white space */
      if (*s!=';' && *s!='\0')
        match=false;
      p++;            /* skip the semicolon in the pattern */
    } else {
      cell ch;
      /* skip whitespace between two non-alphanumeric characters, except
       * for two identical symbols
       */
      assert((char*)p>pattern);
      if (!alphanum(*p) && *(p-1)!=*p)
        while (*s<=' ' && *s!='\0')
          s++;                  /* skip white space */
      ch=litchar(&p,0);         /* this increments "p" */
      if (*s!=ch)
        match=false;
      else
        s++;                    /* this character matches */
    } /* if */
  } /* while */

  if (match && *p=='\0') {
    /* if the last character to match is an alphanumeric character, the
     * current character in the source may not be alphanumeric
     */
    assert(p>(unsigned char*)pattern);
    if (alphanum(*(p-1)) && alphanum(*s))
      match=false;
  } /* if */

  if (match) {
    /* calculate the length of the substituted string */
    int instring=0;
    int stringize;
    size_t len=0;
    for (e=(unsigned char*)substitution; *e!='\0'; e++) {
      if (*e=='#' && *(e+1)=='%' && isdigit(*(e+2)) && !instring) {
        stringize=1;
        e++;            /* skip '#' */
      } else {
        stringize=0;
      } /* if */
      if (*e=='%' && isdigit(*(e+1)) && !instring) {
        arg=*(e+1)-'0';
        assert(arg>=0 && arg<=9);
        assert(stringize==0 || stringize==1);
        if (args[arg]!=NULL)
          len+=strlen((char*)args[arg])+2*stringize;
        else
          len+=2;     /* copy '%' plus digit */
        e++;          /* skip %, digit is skipped later */
      } else {
        if (*e=='"')
          instring=!instring;
        len++;
      } /* if */
    } /* for */
    /* check length of the string after substitution */
    if (strlen((char*)line) + len - (int)(s-line) > buffersize) {
      error(75);      /* line too long */
    } else {
      /* substitute pattern */
      instring=0;
      strdel((char*)line,(int)(s-line));
      for (e=(unsigned char*)substitution,s=line; *e!='\0'; e++) {
        if (*e=='#' && *(e+1)=='%' && isdigit(*(e+2)) && !instring) {
          stringize=1;
          e++;            /* skip '#' */
        } else {
          stringize=0;
        } /* if */
        if (*e=='%' && isdigit(*(e+1)) && !instring) {
          arg=*(e+1)-'0';
          assert(arg>=0 && arg<=9);
          if (args[arg]!=NULL) {
            if (stringize)
              strins((char*)s++,"\"",1);
            strins((char*)s,(char*)args[arg],strlen((char*)args[arg]));
            s+=strlen((char*)args[arg]);
            if (stringize)
              strins((char*)s++,"\"",1);
          } else {
            error(236); /* parameter does not exist, incorrect #define pattern */
            strins((char*)s,(char*)e,2);
            s+=2;
          } /* if */
          e++;          /* skip %, digit is skipped later */
        } else {
          if (*e=='"')
            instring=!instring;
          strins((char*)s,(char*)e,1);
          s++;
        } /* if */
      } /* for */
    } /* if */
  } /* if */

  for (arg=0; arg<10; arg++)
    if (args[arg]!=NULL)
      free(args[arg]);

  return match;
}

static void substallpatterns(unsigned char *line,int buffersize)
{
  unsigned char *start, *end;
  int prefixlen;
  const stringpair *subst;

  start=line;
  while (*start!='\0') {
    /* find the start of a prefix (skip all non-alphabetic characters),
     * also skip strings
     */
    while (!alpha(*start) && *start!='\0') {
      /* skip strings */
      if (is_startstring(start)) {
        start=(unsigned char *)skipstring(start);
        if (*start=='\0')
          break;        /* abort loop on error */
      } /* if */
      start++;          /* skip non-alphapetic character (or closing quote of a string) */
    } /* while */
    if (*start=='\0')
      break;            /* abort loop on error */
    /* if matching the operator "defined", skip it plus the symbol behind it */
    if (strncmp((char*)start,"defined",7)==0 && *(start+7)<=' ') {
      start+=7;         /* skip "defined" */
      /* skip white space & parantheses */
      while (*start<=' ' && *start!='\0' || *start=='(')
        start++;
      /* skip the symbol behind it */
      while (alphanum(*start))
        start++;
      /* drop back into the main loop */
      continue;
    } /* if */
    /* get the prefix (length), look for a matching definition */
    prefixlen=0;
    end=start;
    while (alphanum(*end)) {
      prefixlen++;
      end++;
    } /* while */
    assert(prefixlen>0);
    subst=find_subst((char*)start,prefixlen);
    if (subst!=NULL) {
      /* properly match the pattern and substitute */
      if (!substpattern(start,buffersize-(int)(start-line),subst->first,subst->second))
        start=end;      /* match failed, skip this prefix */
      /* match succeeded: do not update "start", because the substitution text
       * may be matched by other macros
       */
    } else {
      start=end;        /* no macro with this prefix, skip this prefix */
    } /* if */
  } /* while */
}
#endif

/** scanellipsis() looks for '...' in the string and (if not there) in the
 *  remainder of the file.
 *
 *  \param lptr         [in] A pointer to the string to look for the ellipsis.
 *                      This pointer is supposed to point into the source line
 *                      buffer, and at the location where the next token is
 *                      read.
 *
 *  \return `true` if an ellipsis was found and `false` if not
 *
 *  \note On exit, it has restored (or kept intact):
 *  - the current position in the file
 *  - the comment parsing state
 *  - the line buffer used by the lexical analyser
 *  - the active line number and the active file
 */
static bool scanellipsis(const unsigned char *lptr)
{
  /* first look for the ellipsis in the remainder of the string */
  assert(lptr!=NULL && lptr>=srcline);
  while (*lptr<=' ' && *lptr!='\0')
    lptr++;
  if (lptr[0]=='.' && lptr[1]=='.' && lptr[2]=='.')
    return 1;
  if (*lptr!='\0')
    return 0;           /* stumbled on something that is not an ellipsis and not white-space */

  /* the ellipsis was not on the active line, read more lines from the current
   * file (but save its position first)
   */
  if (inpf==NULL || pc_eofsrc(inpf))
    return 0;           /* quick exit: cannot read after EOF */
  unsigned char *localbuf=(unsigned char*)malloc((sLINEMAX+1)*sizeof(unsigned char));
  if (localbuf==NULL)
    return 0;
  long long inpfmark=pc_getpossrc(inpf);
  short localcomment=icomment;

  bool found=false;
  /* read from the file, skip preprocessing, but strip off comments */
  while (!found && pc_readsrc(inpf,localbuf,sLINEMAX)) {
    stripcom(localbuf);
    lptr=localbuf;
    /* skip white space */
    while (*lptr<=' ' && *lptr!='\0')
      lptr++;
    if (lptr[0]=='.' && lptr[1]=='.' && lptr[2]=='.')
      found=true;
    else if (*lptr!='\0')
      break;                       /* stumbled on something that is not an ellipsis and not white-space */
  } /* while */

  /* clean up & reset */
  free(localbuf);
  pc_resetsrc(inpf,inpfmark);
  icomment=localcomment;
  return found;
}

/*  preprocess
 *
 *  Reads a line by readline() into "srcline" and performs basic preprocessing:
 *  deleting comments, skipping lines with false "#if.." code and recognizing
 *  other compiler directives. There is an indirect recursion: lex() calls
 *  preprocess() if a new line must be read, preprocess() calls command(),
 *  which at his turn calls lex() to identify the token.
 *
 *  Global references: lexptr   (modified)
 *                     srcline  (modified)
 *                     freading (read-only)
 */
SC_FUNC void preprocess(void)
{
  int iscommand;

  if (!freading)
    return;
  do {
    readline(srcline,false);
    stripcom(srcline);
    lexptr=srcline;       /* set "line pointer" to start of the parsing buffer */
    iscommand=command();
    if (iscommand!=CMD_NONE)
      errorset(sRESET);   /* reset error flag ("panic mode") on empty line or directive */
#   if !defined NO_DEFINE
      if (iscommand==CMD_NONE) {
        assert(lexptr!=term_expr);
        substallpatterns(srcline,sLINEMAX);
        lexptr=srcline;   /* reset "line pointer" to start of the parsing buffer */
      } /* if */
#   endif
    if (sc_status==statBROWSE && pc_listing && freading
        && (iscommand==CMD_NONE || iscommand==CMD_EMPTYLINE || iscommand==CMD_DIRECTIVE))
    {
      listline++;
      if (pc_curline!=listline) {
        listline=pc_curline;
        setlinedirect(pc_curline);
      } /* if */
      if (iscommand==CMD_EMPTYLINE)
        pc_writeasm(outf,"\n");
      else
        pc_writeasm(outf,(char*)srcline);
    } /* if */
  } while (iscommand!=CMD_NONE && iscommand!=CMD_TERM && freading); /* enddo */
}

static void unpackedstring(const unsigned char *str,int flags)
{
  while (*str!='\0')
    litadd(litchar(&str,flags | UTF8MODE));  /* litchar() alters "str" */
  litadd(0);                    /* terminate string */
}

static void packedstring(const unsigned char *str,int flags)
{
  int i=pc_cellsize-(sCHARBITS/8);    /* start at most significant byte */
  ucell val=0;
  while (*str!='\0') {
    ucell c=litchar(&str,flags);      /* litchar() alters "str" */
    if (c>=(ucell)(1 << sCHARBITS))
      error(43,(long)c);    /* character constant exceeds range */
    val |= (c << 8*i);
    if (i==0) {
      litadd(val);
      val=0;
    }
    i=(i+pc_cellsize-(sCHARBITS/8)) % pc_cellsize;
  }
  /* save last code; make sure there is at least one terminating zero character */
  if (i!=(int)(pc_cellsize-(sCHARBITS/8)))
    litadd(val);            /* at least one zero character in "val" */
  else
    litadd(0);              /* add full cell of zeros */
}

static unsigned long pc_indentmask=0;   /* tab/space interval to make up the current indent */
static unsigned char pc_indentbits=0;   /* bits in pc_indentmask */

SC_FUNC void lex_fetchindent(const unsigned char *string,const unsigned char *pos)
{
  unsigned long mask=1;

  pc_stmtindent=0;
  pc_indentmask=0;
  if (pos-string>255)
    return;
  for (pc_indentbits = 0; pc_indentbits < (unsigned)(pos - string); pc_indentbits++) {
    assert(pc_tabsize>0);
    if (string[pc_indentbits]=='\t') {
      pc_stmtindent += (int)(pc_tabsize - (pc_stmtindent+pc_tabsize) % pc_tabsize);
      pc_indentmask |= mask;
    } else {
      pc_stmtindent++;
      if (pc_matchedtabsize==0)
        pc_matchedtabsize=1;  /* allow auto-detect (spaces on indentation were detected) */
    } /* if */
    mask <<= 1;
  } /* for */
}

SC_FUNC int lex_adjusttabsize(int matchindent)
{
  int tabsize,i;

  if (sc_status!=statBROWSE || pc_stmtindent==matchindent || pc_matchedtabsize!=1
      || pc_indentbits>32)
    return 0; /* no adjustment, because indent already matches, TAB size was
               * already matched, the indent is too complex, or this is not the
               * first pass */

  if (pc_indentmask==0) {
    /* there are no tabs in the current indent (but there is a mismatch, and
     * no TAB size has yet been matched) -> cause: the preceding indented lines
     * all had TABs and this is the first line with spaces => set the TAB size
     * to the biggest multiple of 8, 4, 3 or 2
     */
    if (pc_stmtindent > 16)
      return 0; /* don't guess at deep indenting levels */
    if ((pc_stmtindent % 8)==0)
      tabsize=8;
    else if ((pc_stmtindent % 4)==0)
      tabsize=4;
    else if ((pc_stmtindent % 3)==0)
      tabsize=3;
    else if (pc_stmtindent==2)
      tabsize=2;
    else
      return 0; /* must be 5, 7, 10, 11, 13, 14, 15 */
    pc_tabsize=tabsize;
    pc_matchedtabsize=tabsize;
    return 1;
  } /* if */

  for (tabsize=2; tabsize<=8; tabsize++) {
    int indent=0;
    unsigned long mask=pc_indentmask;
    for (i=0; i<pc_indentbits; i++) {
      if ((mask & 1)!=0)
        indent += (int)(tabsize - (indent+tabsize) % tabsize);
      else
        indent++;
      mask >>= 1;
    } /* for */
    if (indent==matchindent) {
      /* found a match, assume that it is good */
      pc_tabsize=tabsize;
      pc_matchedtabsize=tabsize;
      pc_stmtindent=indent;
      return 1;
    } /* if */
  } /* for */

  return 0;   /* no matching TAB size found */
}

/*  lex(lexvalue,lexsym)        Lexical Analysis
 *
 *  lex() first deletes leading white space, then checks for multi-character
 *  operators, keywords (including most compiler directives), numbers,
 *  labels, symbols and literals (literal characters are converted to a number
 *  and are returned as such). If every check fails, the line must contain
 *  a single-character operator. So, lex() returns this character. In the other
 *  case (something did match), lex() returns the number of the token. All
 *  these tokens have been assigned numbers above 255.
 *
 *  Some tokens have "attributes":
 *     tNUMBER        the value of the number is return in "lexvalue".
 *     tRATIONAL      the value is in IEEE 754 encoding or in fixed point
 *                    encoding in "lexvalue".
 *     tSYMBOL        the first sNAMEMAX characters of the symbol are
 *                    stored in a buffer, a pointer to this buffer is
 *                    returned in "lexsym".
 *     tLABEL         same as tSYMBOL.
 *     tSYMLABEL      same as tSYMBOL, except that the initial '.' is not in
 *                    the "lexsym" buffer.
 *     tSTRING        the string is stored in the literal pool, the index
 *                    in the literal pool to this string is stored in
 *                    "lexvalue".
 *     tPACKSTRING    same as tSTRING
 *
 *  lex() stores all information (the token found and possibly its attribute)
 *  in global variables. This allows a token to be examined twice. If "_pushed"
 *  is true, this information is returned.
 *
 *  Global references: lexptr        (modified)
 *                     litidx        (read-only)
 *                     cached_tok, cached_val, cached_str (modified)
 *                     is_cached     (modified)
 */

static bool is_cached;
static int cached_tok;
static cell cached_val;
static char *cached_str=NULL;
static int _lexnewline; /**< holds whether a newline has just been read */

SC_FUNC int lexinit(int releaseall)
{
  stkidx=0;             /* index for pushstk() and popstk() */
  iflevel=0;            /* preprocessor: nesting of "#if" is currently 0 */
  skiplevel=0;          /* preprocessor: not currently skipping */
  icomment=0;           /* currently not in a multiline comment */
  is_cached=false;      /* no token pushed back into lex */
  _lexnewline=false;

  pc_indentmask=0;      /* tab/space interval to make up the current indent */
  pc_indentbits=0;      /* bits in pc_indentmask */

  if (releaseall) {
    if (srcline!=NULL)
      free(srcline);
    srcline=NULL;
    if (cached_str!=NULL)
      free(cached_str);
    cached_str=NULL;
  } else {
    if (srcline==NULL)
      srcline=(unsigned char*)malloc((sLINEMAX+1)*sizeof(unsigned char));
    if (cached_str==NULL)
      cached_str=(char*)malloc((sLINEMAX+1)*sizeof(char));
    if (srcline==NULL || cached_str==NULL)
      return 0;
    *srcline='\0';
    *cached_str='\0';
  } /* if */
  return 1;
}

static const char *sc_tokens[] = {
         "*=", "/=", "%=", "+=", "-=", "<<=", ">>>=", ">>=", "&=", "&^=", "^=",
         "|=", "||", "&&", "==", "!=", "<=", ">=", "<<", ">>>", ">>", "++", "--",
         "&^", "...", "..", "::",
         "assert", "break", "case", "const", "continue", "default", "defined",
         "do", "else", "exit", "for", "forward", "goto", "if", "native", "new",
         "operator", "public", "return", "sizeof", "sleep", "state", "static",
         "stock", "switch", "tagof", "var", "while",
         "#assert", "#define", "#else", "#elseif", "#endif", "#endinput",
         "#error", "#file", "#if", "#ifdef", "#ifndef", "#include", "#line",
         "#pragma", "#tryinclude", "#undef", "#warning",
         "[semicolon/newline]", "[comma/newline]", "[semicolon/newline]",
         "[integer value]", "[rational value]", "[identifier]", "[label]",
         "[field/parameter reference]", "[string]", "[string]"
       };

SC_FUNC int lex(cell *lexvalue,char **lexstr)
{
  int toolong,newline;
  const unsigned char *starttoken;

  assert(lexvalue!=NULL);
  assert(lexstr!=NULL);
  if (is_cached) {
    is_cached=false;            /* reset "cached" flag set by lexpush() */
    *lexvalue=cached_val;
    *lexstr=cached_str;
    return cached_tok;
  } /* if */

  cached_tok=0;                 /* preset all values */
  cached_val=0;
  cached_str[0]='\0';
  *lexvalue=cached_val;
  *lexstr=cached_str;
  _lexnewline=false;
  if (!freading)
    return 0;

  newline= (lexptr==srcline);   /* does lexptr point to start of line buffer? */
  while (*lexptr<=' ') {        /* delete leading white space */
    if (*lexptr=='\0') {
      preprocess();             /* preprocess resets "lexptr" */
      if (!freading)
        return 0;
      if (lexptr==term_expr)    /* special sequence to terminate a pending expression */
        return (cached_tok=tENDEXPR);
      _lexnewline=true;         /* set this after preprocess(), because
                                 * preprocess() calls lex() recursively */
      newline=true;
    } else {
      lexptr+=1;
    } /* if */
  } /* while */
  if (newline)
    lex_fetchindent(srcline,lexptr);

  int i=tFIRST;
  const char **tokptr=sc_tokens;
  while (i<=tMIDDLE) {  /* match multi-character operators */
    if (*lexptr==**tokptr && match(*tokptr,false)) {
      cached_tok=i;
      strcpy(cached_str,*tokptr);
      if (pc_docexpr)   /* optionally concatenate to documentation string */
        insert_autolist(*tokptr);
      return cached_tok;
    } /* if */
    i+=1;
    tokptr+=1;
  } /* while */
  while (i<=tLAST) {    /* match reserved words and compiler directives */
    if (*lexptr==**tokptr && match(*tokptr,true)) {
      cached_tok=i;
      strcpy(cached_str,*tokptr);
      errorset(sRESET); /* reset error flag (clear the "panic mode")*/
      if (pc_docexpr)   /* optionally concatenate to documentation string */
        insert_autolist(*tokptr);
      return cached_tok;
    } /* if */
    i+=1;
    tokptr+=1;
  } /* while */

  starttoken=lexptr;    /* save start pointer (for concatenating to documentation string) */
  if ((i=number(&cached_val,lexptr))!=0) {   /* number (non-floating point) */
    cached_tok=tNUMBER;
    *lexvalue=cached_val;
    lexptr+=i;
  } else if ((i=ftoi(&cached_val,lexptr))!=0) {
    cached_tok=tRATIONAL;
    *lexvalue=cached_val;
    lexptr+=i;
  } else if (isdigit(*lexptr)) {
    /* so this symbol starts with a digit, but it is not a valid number; flag
     * this and skip the entire symbol
     */
    error(92);          /* invalid number format */
    while (alphanum(*lexptr))
      lexptr++;
    cached_tok=tNUMBER;
    *lexvalue=cached_val=0;
  } else if (alpha(*lexptr) || *lexptr=='.' && alpha(*(lexptr+1))) {  /* symbol, label or symbol-label */
    /*  Note: only sNAMEMAX characters are significant. The compiler
     *        generates a warning if a symbol exceeds this length.
     */
    if (*lexptr=='.') {
      cached_tok=tSYMLABEL;
      lexptr++; /* skip the '.' (it is not stored in the name) */
    } else {
      cached_tok=tSYMBOL;
    } /* if */
    i=0;
    toolong=0;
    while (alphanum(*lexptr)){
      cached_str[i]=(char)*lexptr++;
      if (i<sNAMEMAX)
        i+=1;
      else
        toolong=1;
    } /* while */
    cached_str[i]='\0';
    if (toolong)
      error(200,cached_str,sNAMEMAX); /* symbol too long, truncated to sNAMEMAX chars */
    if (cached_str[0]==PUBLIC_CHAR && cached_str[1]=='\0')
      cached_tok=PUBLIC_CHAR;   /* '@' all alone is not a symbol, it is an operator */
    else if (cached_str[0]=='_' && cached_str[1]=='\0')
      cached_tok='_';           /* '_' by itself is not a symbol, it is a placeholder */
    if (*lexptr==':' && *(lexptr+1)!=':' && (cached_tok!=PUBLIC_CHAR && cached_tok!=tSYMLABEL)) {
      if (sc_allowtags) {
        cached_tok=tLABEL;      /* it wasn't a normal symbol, it was a label/tagname */
        lexptr+=1;              /* skip colon */
      } else if (find_constval(&tagname_tab,cached_str,-1)!=NULL) {
        /* this looks like a tag override (because a tag with this name
         * exists), but tags are not allowed right now, so it is probably an
         * error
         */
        error(81);
      } /* if */
    } /* if */
  } else if (*lexptr=='\"'                                /* packed string literal */
             || *lexptr==ESCAPE_CHAR && *(lexptr+1)=='\"' /* packed plain string */
             || *lexptr=='\'' && *(lexptr+1)=='\''        /* unpacked string */
             || *lexptr=='`' && *(lexptr+1)=='`'          /* unpacked string (alternative syntax) */
             || *lexptr==ESCAPE_CHAR && *(lexptr+1)=='\'' && *(lexptr+2)=='\''  /* unpacked plain string */
             || *lexptr==ESCAPE_CHAR && *(lexptr+1)=='`' && *(lexptr+2)=='`')   /* unpacked plain string */
  {
    int stringflags;
    cached_tok=(*lexptr=='\"') ? tPACKSTRING : tSTRING;
    *lexvalue=cached_val=(cell)litidx;
    cached_str[0]='\0';
    stringflags=-1;     /* to mark the first segment */
    for ( ;; ) {
      char *cat;
      int segmentflags=0;
      if (*lexptr==ESCAPE_CHAR) {
        segmentflags|=RAWMODE;
        lexptr+=1;
      } /* if */
      if (*lexptr=='\"') {
        segmentflags|=ISPACKED;
        lexptr+=1;
      } else {
        assert(*lexptr=='\'' && *(lexptr+1)=='\'' || *lexptr=='`' && *(lexptr+1)=='`');
        lexptr+=2;    /* skip '' */
      } /* if */
      if (stringflags==-1)
        stringflags=segmentflags;
      else if (stringflags!=segmentflags)
        error(238);         /* mixing packed/unpacked/raw strings in concatenation */
      cat=strchr(cached_str,'\0');
      assert(cat!=NULL);
      while ((*lexptr!='"' || (segmentflags & ISPACKED)==0)
             && (*lexptr!='\'' || *(lexptr+1)!='\'' || (segmentflags & ISPACKED)!=0)
             && *lexptr!='\0' && (cat-cached_str)<sLINEMAX)
      {
        if (*lexptr!='\a') {  /* ignore '\a' (which was inserted at a line concatenation) */
          *cat++=*lexptr;
          if (*lexptr==ESCAPE_CHAR && *(lexptr+1)!='\0')
            *cat++=*++lexptr; /* skip escape character plus the escaped character */
        } /* if */
        lexptr++;
      } /* while */
      *cat='\0';            /* terminate string */
      if (*lexptr=='\"') {
        lexptr+=1;          /* skip final quote */
          if ((segmentflags & ISPACKED)==0)
          error(37);        /* wrong string termination */
      } else if (*lexptr=='\'') {
        lexptr+=1;          /* skip first of two final quotes (skip the other after error checking */
          if ((segmentflags & ISPACKED)!=0 || *lexptr!='\'')
          error(37);        /* wrong string termination */
        if (*lexptr=='\'')
          lexptr+=1;
      } else {
        error(37);          /* invalid (non-terminated) string */
      } /* if */
      /* see whether an ellipsis is following the string */
      if (!scanellipsis(lexptr))
        break;              /* no concatenation of string literals */
      /* there is an ellipses, go on parsing (this time with full preprocessing) */
      while (*lexptr<=' ') {
        if (*lexptr=='\0') {
          preprocess();     /* preprocess resets "lexptr" */
          assert(freading && lexptr!=term_expr);
        } else {
          lexptr++;
        } /* if */
      } /* while */
      assert(freading && lexptr[0]=='.' && lexptr[1]=='.' && lexptr[2]=='.');
      lexptr+=3;
      while (*lexptr<=' ') {
        if (*lexptr=='\0') {
          preprocess();     /* preprocess resets "lexptr" */
          assert(freading && lexptr!=term_expr);
        } else {
          lexptr++;
        } /* if */
      } /* while */
      if (!freading || !(*lexptr=='\"'
                         || *lexptr==ESCAPE_CHAR && *(lexptr+1)=='\"'
                         || *lexptr=='\'' && *(lexptr+1)=='\''
                         || *lexptr=='`' && *(lexptr+1)=='`'
                         || *lexptr==ESCAPE_CHAR && *(lexptr+1)=='\'' && *(lexptr+2)=='\''
                         || *lexptr==ESCAPE_CHAR && *(lexptr+1)=='`' && *(lexptr+2)=='`'))
      {
        error(37);                /* invalid string concatenation */
        break;
      } /* if */
    } /* for */
    if ((stringflags & ISPACKED)!=0)
      packedstring((unsigned char*)cached_str,stringflags);
    else
      unpackedstring((unsigned char*)cached_str,stringflags);
  } else if (*lexptr=='\'' || *lexptr=='`') {       /* character literal */
    lexptr+=1;            /* skip quote */
    cached_tok=tNUMBER;
    *lexvalue=cached_val=litchar(&lexptr,UTF8MODE);
    if (*lexptr=='\'') {
      lexptr+=1;          /* skip final quote */
    } else {
      /* try to skip up to the next quote (this may be a user mixing up
       * single and double quotes)
       */
      const unsigned char *ptr=lexptr;
      while (*ptr!='\0' && *ptr!='\'')
        ptr++;
      if (*ptr=='\'') {
        error(makelong(27,2));  /* invalid character constant (or use double quotes) */
        lexptr=ptr+1;     /* skip entire lot */
      } else {
        error(27);        /* invalid character constant (must be one character) */
      } /* if */
    } /* if */
  } else if (*lexptr==';') {      /* semicolon resets "error" flag */
    cached_tok=';';
    lexptr+=1;
    errorset(sRESET);     /* reset error flag (clear the "panic mode")*/
  } else {
    cached_tok=*lexptr;   /* if every match fails, return the character */
    lexptr+=1;            /* advance the "lexptr" pointer */
  } /* if */

  if (pc_docexpr) {     /* optionally concatenate to documentation string */
    char *docstr=(char*)malloc(((int)(lexptr-starttoken)+1)*sizeof(char));
    if (docstr!=NULL) {
      strlcpy(docstr,(char*)starttoken,(int)(lexptr-starttoken)+1);
      insert_autolist(docstr);
      free(docstr);
    } /* if */
  } /* if */
  return cached_tok;
}

/*  lexpush
 *
 *  Pushes a token back, so the next call to lex() will return the token
 *  last examined, instead of a new token.
 *
 *  Only one token can be pushed back.
 *
 *  In fact, lex() already stores the information it finds into global
 *  variables, so all that is to be done is set a flag that informs lex()
 *  to read and return the information from these variables, rather than
 *  to read in a new token from the input file.
 */
SC_FUNC void lexpush(void)
{
  assert(!is_cached); /* token should not already be pushed */
  is_cached=true;
}

/** lexclr() clears the "cached" flag for the token to make sure lex() will read
 *  in a new symbol (a not continue with some old one), and it also skips
 *  anything remaining on the current source line. This is required to avoid
 *  preprocessor expressions to spill over into code, and in a few cases after
 *  detecting an syntax error.
 */
SC_FUNC void lexclr(int clreol)
{
  is_cached=false;
  if (clreol) {
    lexptr=(unsigned char*)strchr((char*)srcline,'\0');
    assert(lexptr!=NULL);
  } /* if */
}

/* lexpeek()
 * Returns the next token without removing it
 */
SC_FUNC int lexpeek(void)
{
  cell val;
  char *str;
  int tok=lex(&val,&str);
  lexpush();
  return tok;
}

/* Changes the current token, this is sometimes convenient when a
 * reserved word must be re-interpreted as a symbol.
 */
SC_FUNC int lexsettoken(int token,const char *lexsym)
{
  assert(is_cached==false);
  cached_tok=token;
  assert(lexsym!=NULL);
  assert(strlen(lexsym)<=sLINEMAX);
  strcpy(cached_str,lexsym);
  return cached_tok;
}

/** matchtoken() checks whether the next token matches the requested one. If
 *  the token differs from the one expected, it is pushed back.
 *
 *  This function returns 1 for "token found" and 2 for "implied statement
 *  termination token" found --the statement termination is an end of line in an
 *  expression where there is no pending operation. Such an implied token (i.e.
 *  not present in the source code) should not be pushed back, which is why it
 *  is sometimes important to distinguish the two.
 */
SC_FUNC int matchtoken(int token)
{
  cell val;
  char *str;
  int tok=lex(&val,&str);
  if (tok==token
      || token==tTERM && (tok==';' || tok==tENDEXPR)
      || token==tSEPARATOR && tok==',')
  {
    return 1;
  } else if ((token==tTERM || token==tSEPARATOR)
             && (_lexnewline || !freading))
  {
    /* Push "tok" back, because it is the token following the implicit statement
     * termination (newline) token.
     */
    lexpush();
    return 2;
  } else {
    lexpush();
    return 0;
  } /* if */
}

/** tokeninfo() returns additional information of a token after using
 *  matchtoken() or needtoken(). It does no harm using this routine after a call
 *  to lex(), but lex() already returns the same information.
 *
 *  \param val          [out, optional] Set to the cached numeric value,
 *                      as parsed by the earlier call to lex(). This parameter
 *                      may be set to `NULL`.
 *  \param str          [out, optional] Set to the cached text, as parsed by the
 *                      earlier call to lex(). This parameter may be set to
 *                      `NULL`.
 *
 *  \return The cached token value (normally, this one is already known).
 */
SC_FUNC int tokeninfo(cell *val,char **str)
{
  /* if the token was pushed back, tokeninfo() would return the token and
   * parameters of the *next* token, not of the *current* token.
   */
  assert(!is_cached);
  if (val)
    *val=cached_val;
  if (str)
    *str=cached_str;
  return cached_tok;
}

/*  needtoken
 *
 *  This routine checks for a required token and gives an error message if
 *  it isn't there (and returns 0/false in that case). Like function matchtoken(),
 *  this function returns 1 for "token found" and 2 for "statement termination
 *  token" found; see function matchtoken() for details.
 *
 *  Global references: cached_tok;
 */
SC_FUNC int needtoken(int token)
{
  int t=matchtoken(token);
  if (t!=1 && !(t==2 && token==tTERM)) {
    char s1[sNAMEMAX+1],s2[sNAMEMAX+1];
    assert(is_cached);                    /* token already pushed back */
    litidx=0;                             /* force clear literal pool on error */
    if (token<256)
      snprintf(s1,countof(s1),"%c",(char)token);        /* single character token */
    else
      strlcpy(s1,sc_tokens[token-tFIRST],countof(s1));  /* multi-character symbol */
    if (!freading)
      strlcpy(s2,"-end of file-",countof(s2));
    else if (cached_tok<256)
      snprintf(s2,countof(s2),"%c",(char)cached_tok);
    else if (cached_tok==tNUMBER)
      snprintf(s2,countof(s2),"%d",(int)cached_val);
    else if (cached_tok==tSYMBOL)
      strlcpy(s2,cached_str,countof(s2));
    else
      strlcpy(s2,sc_tokens[cached_tok-tFIRST],countof(s2));
    error(1,s1,s2);     /* expected ..., but found ... */
  } /* if */
  return t;
}

/** skiptotoken() reads and ignores tokens until matching the specified one,
 *  or until finding a ';' or end-of-line. The goal of this function is to find
 *  a "restart" point after flagging an error, so one error does not cause an
 *  avalance of follow-up errors.
 *
 *  \param token      The token to match.
 *  \param pushback   If `true`, and if the token is found, the token is pushed
 *                    back for later matching; otherwise it is also skipped.
 */
SC_FUNC bool skiptotoken(int token,bool pushback)
{
  int tok;
  do {
    cell val;
    char *str;
    tok=lex(&val,&str);
    if (tok==token) {
      if (pushback)
        lexpush();
      return true;
    } /* if */
  } while (tok!=';' && tok!=tENDEXPR && !_lexnewline && freading);
  if (_lexnewline || (tok==';' && token!=tTERM))
    lexpush();
  return false;
}

/** match() compares a series of characters from the input file with the
 *  characters in 'keyword' (that contains a token). If the token on the input
 *  file matches 'keyword', the input file pointer 'lexptr' is adjusted to point
 *  to the next token, otherwise 'lexptr' remains unaltered.
 *
 *  If the parameter 'need_nonalpha' is true, match() requires that the first
 *  character behind the recognized token is non-alphanumeric.
 *
 *  Global references: lexptr (modified)
 */
static bool match(const char *keyword,bool need_nonalpha)
{
  const unsigned char *ptr=lexptr;
  if (*keyword=='#' && *ptr=='#') {
    /* allow whitespace between the '#' and the keyword */
    keyword++;
    ptr++;
    while (*ptr<=' ' && *ptr!='\0')
      ptr++;
  } /* if */
  while (*keyword) {
    if ((unsigned char)*keyword!=*ptr)
      return false;
    keyword++;
    ptr++;
  } /* while */
  if (need_nonalpha && alphanum(*ptr))  /* symbol must terminate with non-alphanumeric char */
    return false;
  lexptr=ptr;           /* match found, skip symbol */
  return true;
}

static unsigned litq_sz=0;      /**< current size of the literal table */

SC_FUNC void litq_free(void)
{
  if (litq!=NULL)
    free(litq);
  litq=NULL;
  litq_sz=0;
}

static void chk_grow_litq(void)
{
  assert(litidx<=litq_sz);
  if (litidx>=litq_sz) {
    size_t newsize= (litq_sz==0) ? sDEF_LITMAX : 2*litq_sz;
    cell *p=(cell *)malloc(newsize*sizeof(cell));
    if (p==NULL)
      error(102,"literal table");   /* literal table overflow (fatal error) */
    if (litq!=NULL) {
      memcpy(p,litq,litq_sz*sizeof(cell));
      free(litq);
    } /* if */
    litq=p;
    litq_sz=newsize;
  } /* if */
}

/*  litadd
 *
 *  Adds a value at the end of the literal queue. The literal queue is used
 *  for literal strings used in functions and for initializing array variables.
 *
 *  Global references: litidx  (modified)
 *                     litq    (modified)
 */
SC_FUNC void litadd(cell value)
{
  chk_grow_litq();
  assert(litidx<litq_sz);
  litq[litidx++]=value;
}

/*  litinsert
 *
 *  Inserts a value into the literal queue. This is sometimes necessary for
 *  initializing multi-dimensional arrays.
 *
 *  Global references: litidx  (modified)
 *                     litq    (modified)
 */
SC_FUNC void litinsert(cell value,unsigned pos)
{
  chk_grow_litq();
  assert(litidx<litq_sz);
  assert(pos<=litidx);
  memmove(litq+(pos+1),litq+pos,(litidx-pos)*sizeof(cell));
  litidx++;
  litq[pos]=value;
}

/*  litremove
 *
 *  Removes an entry in the literal queue (to undo an earlier addition.
 */
SC_FUNC void litremove(unsigned pos)
{
  assert(litidx>0 && litidx<litq_sz);
  assert(pos<litidx);
  memmove(litq+pos,litq+(pos+1),(litidx-pos-1)*sizeof(cell));
  litidx--;
}

/*  litchar
 *
 *  Return current literal character and increase the pointer to point
 *  just behind this literal character.
 *
 *  Note: standard "escape sequences" are suported, but the backslash may be
 *        replaced by another character; the syntax '\ddd' is supported,
 *        but ddd must be decimal!
 */
static cell litchar(const unsigned char **lptr,int flags)
{
  cell c=0;
  const unsigned char *cptr;

  cptr=*lptr;
  if ((flags & RAWMODE)!=0 || *cptr!=ESCAPE_CHAR) {  /* no escape character */
#   if !defined PAWN_NO_UTF8
      if (sc_is_utf8 && (flags & UTF8MODE)!=0) {
        c=get_utf8_char(cptr,&cptr);
        assert(c>=0);   /* file was already scanned for conformance to UTF-8 */
      } else {
#   endif
#     if !defined PAWN_NO_CODEPAGE
        c=cp_translate(cptr,&cptr);
#     else
        c=*cptr;
        cptr+=1;
#     endif
#   if !defined PAWN_NO_UTF8
      } /* if */
#   endif
  } else {
    cptr+=1;
    if (*cptr==ESCAPE_CHAR) {
      c=*cptr;          /* \\ == \ (the escape character itself) */
      cptr+=1;
    } else {
      switch (*cptr) {
      case 'a':         /* \a == audible alarm */
        c=7;
        cptr+=1;
        break;
      case 'b':         /* \b == backspace */
        c=8;
        cptr+=1;
        break;
      case 'e':         /* \e == escape */
        c=27;
        cptr+=1;
        break;
      case 'f':         /* \f == form feed */
        c=12;
        cptr+=1;
        break;
      case 'n':         /* \n == NewLine character */
        c=10;
        cptr+=1;
        break;
      case 'r':         /* \r == carriage return */
        c=13;
        cptr+=1;
        break;
      case 't':         /* \t == horizontal TAB */
        c=9;
        cptr+=1;
        break;
      case 'v':         /* \v == vertical TAB */
        c=11;
        cptr+=1;
        break;
      case 'x':
        cptr+=1;
        c=0;
        while (ishex(*cptr)) {
          if (isdigit(*cptr))
            c=(c<<4)+(*cptr-'0');
          else
            c=(c<<4)+(tolower(*cptr)-'a'+10);
          cptr++;
        } /* while */
        if (*cptr==';')
          cptr++;       /* swallow a trailing ';' */
        break;
      case '\'':        /* \' == ' (single quote) */
      case '"':         /* \" == " (double quote) */
      case '%':         /* \% == % (percent) */
        c=*cptr;
        cptr+=1;
        break;
      default:
        if (isdigit(*cptr)) {   /* \ddd */
          c=0;
          while (*cptr>='0' && *cptr<='9')  /* decimal! */
            c=c*10 + *cptr++ - '0';
          if (*cptr==';')
            cptr++;     /* swallow a trailing ';' */
        } else {
          error(27);    /* invalid character constant */
        } /* if */
      } /* switch */
    } /* if */
  } /* if */
  *lptr=cptr;
  assert(c>=0);
  return c;
}

/*  alpha
 *
 *  Test if character "c" is alphabetic ("a".."z"), an underscore ("_")
 *  or an "at" sign ("@"). The "@" is an extension to standard C.
 */
static bool alpha(unsigned char c)
{
  return (isalpha(c) || c=='_' || c==PUBLIC_CHAR);
}

/*  alphanum
 *
 *  Test if character "c" is alphanumeric ("a".."z", "0".."9", "_" or "@")
 */
SC_FUNC int alphanum(unsigned char c)
{
  return (alpha(c) || isdigit(c));
}

/*  ishex
 *
 *  Test if character "c" is a hexadecimal digit ("0".."9" or "a".."f").
 */
SC_FUNC int ishex(char c)
{
  return (c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F');
}

/* The local variable table must be searched backwards, so that the deepest
 * nesting of local variables is searched first. The simplest way to do
 * this is to insert all new items at the head of the list.
 * In the global list, the symbols are kept in sorted order, so that the
 * public functions are written in sorted order.
 */
static symbol *add_symbol(symbol *root,const symbol *entry,int sort)
{
  symbol *newsym;

  if (sort)
    while (root->next!=NULL && strcmp(entry->name,root->next->name)>0)
      root=root->next;

  if ((newsym=(symbol *)malloc(sizeof(symbol)))==NULL) {
    error(103);
    return NULL;
  } /* if */
  memcpy(newsym,entry,sizeof(symbol));
  newsym->next=root->next;
  root->next=newsym;
  return newsym;
}

static void free_symbol(symbol *sym)
{
  /* free all sub-symbol allocated memory blocks, depending on the
   * kind of the symbol
   */
  assert(sym!=NULL);
  if (sym->ident==iFUNCTN) {
    arginfo *arg;
    int idx;
    /* run through the argument list; "default array" arguments
     * must be freed explicitly; the tag list must also be freed
     */
    assert(sym->dim.arglist!=NULL);
    for (arg=sym->dim.arglist; arg->ident!=0; arg++) {
      if (arg->ident==iREFARRAY && arg->hasdefault)
        free(arg->defvalue.array.data);
      else if (arg->ident==iVARIABLE
               && ((arg->hasdefault & uSIZEOF)!=0 || (arg->hasdefault & uTAGOF)!=0))
        free(arg->defvalue.size.symname);
      assert(arg->tags!=NULL);
      free(arg->tags);
      for (idx=0; idx<arg->numdim; idx++) {
        if (arg->dimnames[idx]!=NULL) {
          delete_consttable(arg->dimnames[idx]);
          free(arg->dimnames[idx]);
        } /* if */
      } /* for */
    } /* for */
    free(sym->dim.arglist);
    if (sym->states!=NULL) {
      delete_statelisttable(sym->states);
      free(sym->states);
    } /* if */
  } else if (sym->ident==iVARIABLE || sym->ident==iARRAY || sym->ident==iREFARRAY) {
    if (sym->states!=NULL) {
      delete_statelisttable(sym->states);
      free(sym->states);
    } /* if */
    /* free index name list of an array */
    if (sym->dim.array.names!=NULL) {
      delete_consttable(sym->dim.array.names);
      free(sym->dim.array.names);
    } /* if */
  } /* if */
  assert(sym->refer!=NULL);
  free(sym->refer);
  if (sym->documentation!=NULL)
    free(sym->documentation);
  free(sym);
}

SC_FUNC void delete_symbol(symbol *root,symbol *sym)
{
# if !defined NDEBUG
    /* no symbols should hold a reference to the to-be-deleted symbol anymore */
    for (symbol *scan=root->next; scan!=NULL; scan=scan->next) {
      assert(scan->refer!=NULL);
      for (int i=0; i<scan->numrefers; i++)
        assert(scan->refer[i]!=sym);
    } /* for */
# endif

  /* find the symbol that comes just before the to-be-deleted symbol
   * (this function assumes that you will never delete a symbol that is not
   * in the table pointed at by "root")
   */
  assert(root!=sym);
  while (root->next!=sym) {
    root=root->next;
    assert(root!=NULL);
  } /* while */

  /* unlink it, then free it */
  root->next=sym->next;
  free_symbol(sym);
}

SC_FUNC void delete_symbols(symbol *root,int level,bool delete_labels,bool delete_functions)
{
  symbol *base;
  symbol *sym,*parent_sym,*child_sym;
  statelist *stateptr;
  bool mustdelete;

  /* erase only the symbols with a deeper nesting level than the
   * specified nesting level
   */
  base=root;
  while (base->next!=NULL) {
    sym=base->next;
    if (sym->block<level)
      break;
    if ((sym->usage & uVISITED)!=0) {
      base=sym;                 /* skip the symbol */
      continue;
    } /* if */
    if ((sym->flags & flgARGUMENT)!=0 && !delete_labels) {
      base=sym;                 /* arguments, like labels, go with the function */
      continue;
    } /* if */
    switch (sym->ident) {
    case iLABEL:
      mustdelete=delete_labels;
      break;
    case iVARIABLE:
    case iARRAY:
      /* do not delete global variables if functions are preserved */
      mustdelete=delete_functions;
      break;
    case iREFERENCE:
      /* always delete references (only exist as function parameters) */
      mustdelete=true;
      break;
    case iREFARRAY:
      /* a global iREFARRAY symbol is the return value of a function: delete
       * this only if functions must be deleted; other iREFARRAY instances
       * (locals) are also deleted
       */
      mustdelete=delete_functions;
      for (parent_sym=sym->parent; parent_sym!=NULL && parent_sym->ident!=iFUNCTN; parent_sym=parent_sym->parent)
        assert(parent_sym->ident==iREFARRAY);
      assert(parent_sym==NULL || (parent_sym->ident==iFUNCTN && parent_sym->parent==NULL));
      if (parent_sym==NULL || parent_sym->ident!=iFUNCTN)
        mustdelete=true;
      break;
    case iCONSTEXPR:
      /* delete constants, except predefined constants */
      mustdelete=delete_functions || (sym->usage & uPREDEF)==0;
      break;
    case iFUNCTN:
      /* optionally preserve globals (variables & functions), but
       * NOT native functions
       */
      mustdelete=delete_functions || (sym->usage & uNATIVE)!=0;
      assert(sym->parent==NULL);
      break;
    case iARRAYCELL:
    case iARRAYCHAR:
    case iEXPRESSION:
    case iVARARGS:
    default:
      assert(0);
      mustdelete=false; /* dummy assignment, to avoid warnings by lint-like checkers */
      break;
    } /* switch */
    if (mustdelete) {
      /* first delete children, if any */
      int count=0;
      while ((child_sym=finddepend(sym))!=NULL) {
        delete_symbol(root,child_sym);
        count++;
      } /* while */
      if (count==0) {
        base->next=sym->next;
        free_symbol(sym);
      } else {
        /* chain has changed */
        delete_symbol(root,sym);
        base=root;      /* restart */
      } /* if */
    } else {
      /* if the function was prototyped, but not implemented in this source,
       * mark it as such, so that its use can be flagged
       */
      if (sym->ident==iFUNCTN && (sym->usage & uDEFINE)==0)
        sym->usage |= uMISSING;
      if (sym->ident==iFUNCTN || sym->ident==iVARIABLE || sym->ident==iARRAY)
        sym->usage &= ~uDEFINE; /* clear "defined" flag */
      /* set all states as "undefined" too */
      if (sym->states!=NULL)
        for (stateptr=sym->states->next; stateptr!=NULL; stateptr=stateptr->next)
          stateptr->addr=0;
      /* for user defined operators, also remove the "prototyped" flag, as
       * user-defined operators *must* be declared before use
       */
      if (sym->ident==iFUNCTN && !alpha(*sym->name))
        sym->usage &= ~uPROTOTYPED;
      /* mark the symbol as "visited", so we won't process it twice */
      sym->usage |= uVISITED;
      base=sym;                 /* skip the symbol */
    } /* if */
  } /* while */

  /* go through the symbols again to erase any "visited" marks */
  for (sym=root->next; sym!=NULL; sym=sym->next)
    sym->usage &= ~uVISITED;
}

/* The purpose of the hash is to reduce the frequency of a "name"
 * comparison (which is costly). There is little interest in avoiding
 * clusters in similar names, which is why this function is plain simple.
 */
SC_FUNC uint32_t namehash(const char *name)
{
  const unsigned char *ptr=(const unsigned char *)name;
  uint32_t len=(uint32_t)strlen(name);
  if (len==0)
    return 0L;
  assert(len<256);
  return (len<<24Lu) + (ptr[0]<<16Lu) + (ptr[len-1]<<8Lu) + (ptr[len>>1Lu]);
}

/** find_symbol() looks up a symbol.
 *  \param root       [in] the symbol table (typically glbtab or loctab).
 *  \param name       [in] the name of the symbol.
 *  \param fnumber    The file number for symbols with static scope, or -1 for
 *                    the local symbol table.
 *  \param stateid    The ID of the state-list to match, or 0 if none or not
 *                    relevant.
 */
static symbol *find_symbol(const symbol *root,const char *name,int fnumber,int stateid)
{
  symbol *sym=root->next;
  unsigned long hash=namehash(name);
  int automaton=(stateid>0) ? state_getfsa(stateid) : -1;
  while (sym!=NULL) {
    if (hash==sym->hash && strcmp(name,sym->name)==0            /* check name */
        && sym->parent==NULL                                    /* sub-types (hierarchical types) are skipped */
        && (sym->fvisible==NOT_SET || sym->fvisible==fnumber))  /* check file number for scope */
    {
      /* we found a symbol, now check for a few options:
       * 1) it is a function (ignore stateid because a stub is generated);
       * 2) it is a variable without states and we are filtering out variables
       *    with automaton scope;
       * 3) we search for variables with automaton scope and this variable has
       *    states and in a matching automaton; now we also need to verify
       *    whether there is an intersection between the symbol's state-list
       *    and the state-list for the `stateid` parameter
       */
      assert(sym->states==NULL || sym->states->next!=NULL);     /* first element of the state list is the "root" */
      if (sym->ident==iFUNCTN                                   /* function with or without states (because a stub is generated) */
          || automaton<0 && sym->states==NULL                   /* global variable (without states, not part of an automaton) */
          || automaton>=0 && sym->states!=NULL && state_getfsa(sym->states->next->id)==automaton && state_conflict_id(stateid,sym->states->next->id))
        return sym;   /* return first match (for local variables which may shadow thos at a higher level)*/
    } /* if */
    sym=sym->next;
  } /* while */
  return NULL;
}

static symbol *find_symbol_child(const symbol *root,const symbol *sym)
{
  symbol *ptr=root->next;
  while (ptr!=NULL) {
    if (ptr->parent==sym)
      return ptr;
    ptr=ptr->next;
  } /* while */
  return NULL;
}

/* Adds "bywhom" to the list of referrers of "entry". Typically,
 * bywhom will be the function that uses a variable or that calls
 * the function.
 */
SC_FUNC int refer_symbol(symbol *entry,symbol *bywhom)
{
  int count;

  assert(bywhom!=NULL);         /* it makes no sense to add a "void" referrer */
  assert(entry!=NULL);
  assert(entry->refer!=NULL);

  /* see if it is already there */
  for (count=0; count<entry->numrefers && entry->refer[count]!=bywhom; count++)
    /* nothing */;
  if (count<entry->numrefers) {
    assert(entry->refer[count]==bywhom);
    return true;
  } /* if */

  /* see if there is an empty spot in the referrer list */
  for (count=0; count<entry->numrefers && entry->refer[count]!=NULL; count++)
    /* nothing */;
  assert(count <= entry->numrefers);
  if (count==entry->numrefers) {
    symbol **refer;
    int newsize=2*entry->numrefers;
    assert(newsize>0);
    /* grow the referrer list */
    refer=(symbol**)realloc(entry->refer,newsize*sizeof(symbol*));
    if (refer==NULL)
      return false;             /* insufficient memory */
    /* initialize the new entries */
    entry->refer=refer;
    for (count=entry->numrefers; count<newsize; count++)
      entry->refer[count]=NULL;
    count=entry->numrefers;     /* first empty spot */
    entry->numrefers=newsize;
  } /* if */

  /* add the referrer */
  assert(entry->refer[count]==NULL);
  entry->refer[count]=bywhom;
  return true;
}

SC_FUNC void markusage(symbol *sym,short usage)
{
  assert(sym!=NULL);
  sym->usage |= usage;
  if ((usage & uWRITTEN)!=0 && (sym->ident==iVARIABLE || sym->ident==iARRAY))
    sym->x.lnumber_write=pc_curline;
  /* check if (global) reference must be added to the symbol */
  if ((usage & (uREAD | uWRITTEN))!=0) {
    /* only do this for global symbols */
    if (sym->scope==sGLOBAL) {
      /* "curfunc" should always be valid, since statements may not occur
       * outside functions; in the case of syntax errors, however, the
       * compiler may arrive here this function with a NULL "curfunc"
       */
      if (curfunc!=NULL)
        refer_symbol(sym,curfunc);
    } /* if */
  } /* if */
}

/** findglb() returns a pointer to the global symbol (if found) or `NULL` (if
 *  not found).
 *
 *  \param name         [in] The variable/function name.
 *  \param filter       If set to `sGLOBAL`, it looks for a variable/function
 *                      with no states attached; if set to `sSTATEVAR` it tries
 *                      to first find a variable/function with a state that
 *                      matches the current state & automaton (and if that
 *                      fails, it drops back to `sGLOBAL`).
 *
 *  \return The matching symbol, or `NULL` if none found.
 */
SC_FUNC symbol *findglb(const char *name,int filter)
{
  assert(filter==sGLOBAL || filter==sSTATEVAR);

  /* first check for a symbol whose state-list matches the current automaton
   * and has overlapping state
   */
  symbol *sym=NULL;
  if (filter>sGLOBAL && sc_curstates>0)
     sym=find_symbol(&glbtab,name,fcurrent,sc_curstates);

  /* if no symbol with a matching automaton exists, find a variable/function
   * that has no state(s) attached to it
   */
  if (sym==NULL)
    sym=find_symbol(&glbtab,name,fcurrent,0);
  return sym;
}

/** findloc() returns a pointer to the local symbol (if found) or `NULL` (if not
 *  found). See add_symbol() how the deepest nesting level is searched first.
 */
SC_FUNC symbol *findloc(const char *name)
{
  return find_symbol(&loctab,name,-1,0);
}

SC_FUNC symbol *findconst(const char *name)
{
  symbol *sym;

  sym=find_symbol(&loctab,name,-1,0);       /* try local symbols first */
  if (sym==NULL || sym->ident!=iCONSTEXPR)  /* not found, or not a constant */
    sym=find_symbol(&glbtab,name,fcurrent,0);
  if (sym==NULL || sym->ident!=iCONSTEXPR)
    return NULL;
  assert(sym->parent==NULL);                /* constants have no hierarchy */
  return sym;
}

SC_FUNC symbol *finddepend(const symbol *parent)
{
  symbol *sym;

  sym=find_symbol_child(&loctab,parent);    /* try local symbols first */
  if (sym==NULL)                            /* not found */
    sym=find_symbol_child(&glbtab,parent);
  return sym;
}

/*  addsym
 *
 *  Adds a symbol to the symbol table (either global or local variables,
 *  or global and local constants).
 */
SC_FUNC symbol *addsym(const char *name,cell addr,int ident,int scope,int block,int tag,int usage)
{
  symbol entry;
  symbol **refer;

  assert(name!=NULL);

  /* labels may only be defined once */
  assert(ident!=iLABEL || findloc(name)==NULL);

  /* create an empty referrer list */
  if ((refer=(symbol**)malloc(sizeof(symbol*)))==NULL) {
    error(103);         /* insufficient memory */
    return NULL;
  } /* if */
  *refer=NULL;

  /* first fill in the entry */
  memset(&entry,0,sizeof entry);
  strcpy(entry.name,name);
  entry.hash=namehash(name);
  entry.addr=addr;
  entry.codeaddr=code_idx;
  entry.scope=(short)scope;
  entry.block=(short)block;
  entry.ident=(short)ident;
  entry.tag=tag;
  entry.usage=(short)usage;
  entry.fvisible=NOT_SET; /* assume global visibility (ignored for local symbols) */
  entry.fnumber=fcurrent;
  entry.lnumber=pc_curline;
  entry.numrefers=1;
  entry.refer=refer;

  /* then insert it in the list */
  if (scope==sGLOBAL)
    return add_symbol(&glbtab,&entry,true);
  else
    return add_symbol(&loctab,&entry,false);
}

SC_FUNC symbol *addvariable(const char *name,cell addr,int ident,int scope,int block,
                            int tag,const int dim[],constvalue *dimnames[],int numdim,int usage)
{
  symbol *sym;

  /* global variables may only be defined once
   * One complication is that functions returning arrays declare an array
   * with the same name as the function, so the assertion must allow for
   * this special case. Another complication is that variables may be
   * "redeclared" if they are local to an automaton (and findglb() will find
   * the symbol without states if no symbol with states exists).
   */
# if !defined NDEBUG
    sym=findglb(name,sGLOBAL);
    assert(scope!=sGLOBAL || sym==NULL || (sym->usage & uDEFINE)==0
           || sym->ident==iFUNCTN && (sym==curfunc || (sym->usage & uNATIVE)!=0)
           || sym->states==NULL && sc_curstates>0);
# endif

  if (ident==iARRAY || ident==iREFARRAY) {
    symbol *parent=NULL,*top;
    int level;
    sym=NULL;                   /* to avoid a compiler warning */
    for (level=0; level<numdim; level++) {
      top=addsym(name,addr,ident,scope,block,tag,uDEFINE);
      top->dim.array.length=dim[level];
      top->dim.array.level=(short)(numdim-level-1);
      top->dim.array.names=dimnames[level];
      top->parent=parent;
      if ((usage & uPACKED) && level==numdim-1)
        top->usage|=uPACKED;
      parent=top;
      if (level==0)
        sym=top;
    } /* for */
  } else {
    sym=addsym(name,addr,ident,scope,block,tag,uDEFINE);
  } /* if */
  return sym;
}

/*  getlabel
 *
 *  Returns te next internal label number. The global variable sc_labnum is
 *  initialized to zero.
 */
SC_FUNC int getlabel(void)
{
  return sc_labnum++;
}

/** celltohex() converts a number to a hexadecimal string and returns a pointer
 *  to that string.
 *
 *  \param val          The value to convert to text.
 *
 *  \return A pointer to a static array with the text representation of the
 *          value.
 *
 *  \note This function is *not* re-entrant.
 */
SC_FUNC char *celltohex(ucell val)
{
static char hexstr[20];
  int nibble[16];       /* a 64-bit hexadecimal cell has 16 nibbles */

  unsigned imax=pc_cellsize*2;
  assert(imax<=countof(nibble));
  assert(imax<countof(hexstr));
  for (unsigned i=0; i<imax; i+=1){
    nibble[i]=(int)(val & 0x0f);        /* nibble 0 is lowest nibble */
    val>>=4;
  } /* for */
  /* reverse the nibbles in the string */
  char *ptr=hexstr;
  unsigned i=imax;
  while (i>0) {
    i-=1;
    if (nibble[i]>=10)
      *ptr++=(char)('a'+(nibble[i]-10));
    else
      *ptr++=(char)('0'+nibble[i]);
  } /* while */
  *ptr='\0';            /* and a zero-terminator */
  return hexstr;
}

