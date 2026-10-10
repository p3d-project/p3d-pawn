/*  Pawn compiler - Binary code generation (the "assembler")
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
 *  Version: $Id: sc6.c 7709 2026-10-10 19:44:52Z thiadmer $
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>     /* for macro max() */
#include <stddef.h>     /* for macro offsetof() */
#include <string.h>
#include <ctype.h>
#if defined FORTIFY
# include <alloc/Fortify.h>
#endif
#include "lstring.h"
#include "sc.h"
#include "../amx/amxdbg.h"
#if defined PAWN_KEELOQ
# include "../amx/keeloq.c"
#endif
#if defined __LINUX__ || defined __FreeBSD__ || defined __OpenBSD__ || defined __APPLE__
# include <sclinux.h>
#endif


static void append_dbginfo(FILE *fout);


typedef cell (*OPCODE_PROC)(FILE *fbin,const char *params,cell opcode,cell cip);

typedef struct {
  cell opcode;
  char *name;
  int segment;          /* sIN_CSEG=parse in cseg, sIN_DSEG=parse in dseg */
  OPCODE_PROC func;
  short opt_level;      /* optimization level for this instruction set */
  short seqpoint;       /* registers for which the instruction is a sequence point (bit field) */
} OPCODE;

#define SEQPOINT(reg)   (1u << (reg))

static cell *lbltab;    /* label table */
static int writeerror;

static char *skipwhitespace(const char *str)
{
  while (isspace(*str))
    str++;
  return (char*)str;
}

/* Watcom C/C++: strtol() does apparently not work correctly on very large hexadecimal values */
SC_FUNC ucell hex2ucell(const char *s,const char **n)
{
  /* ignore leading whitespace */
  s=skipwhitespace(s);
  assert(*s!='\0');

  /* allow a negation sign to create the two's complement of numbers */
  bool negate=false;
  if (*s=='-') {
    negate=true;
    s++;
  } /* if */

  assert((*s>='0' && *s<='9') || (*s>='a' && *s<='f') || (*s>='A' && *s<='F'));
  int nibbles=0;
  ucell result=0;
  for ( ;; ) {
    int digit;
    if (*s>='0' && *s<='9')
      digit=*s-'0';
    else if (*s>='a' && *s<='f')
      digit=*s-'a' + 10;
    else if (*s>='A' && *s<='F')
      digit=*s-'A' + 10;
    else
      break;            /* probably whitespace */
    result=(result<<4) | digit;
    nibbles++;
    s++;
  } /* for */
  if (n!=NULL)
    *n=(char*)s;

  if (negate)
    result=~result+1;   /* take two's complement of the result */
  /* truncate to cell size */
  if (pc_cellsize==2)
    result&=0xffffu;
  else if (pc_cellsize==4)
    result&=0xffffffffu;

  return result;
}

SC_FUNC ucell getparamvalue(const char *s,const char **n)
{
  ucell result=0;
  for ( ;; ) {
    result+=hex2ucell(s,&s);
    if (*s!='+')  /* handle '+', see sc7.scp */
      break;
    s++;
  } /* for */
  if (n!=NULL)
    *n=(char*)s;
  return result;
}

#if BYTE_ORDER==BIG_ENDIAN
static uint16_t *align16(uint16_t *v)
{
  unsigned char *s = (unsigned char *)v;
  unsigned char t;

  /* swap two bytes */
  t=s[0];
  s[0]=s[1];
  s[1]=t;
  return v;
}

static uint32_t *align32(uint32_t *v)
{
  unsigned char *s = (unsigned char *)v;
  unsigned char t;

  /* swap outer two bytes */
  t=s[0];
  s[0]=s[3];
  s[3]=t;
  /* swap inner two bytes */
  t=s[1];
  s[1]=s[2];
  s[2]=t;
  return v;
}

#if PAWN_CELL_SIZE>=64
static uint64_t *align64(uint64_t *v)
{
  unsigned char *s = (unsigned char *)v;
  unsigned char t;

  t=s[0];
  s[0]=s[7];
  s[7]=t;

  t=s[1];
  s[1]=s[6];
  s[6]=t;

  t=s[2];
  s[2]=s[5];
  s[5]=t;

  t=s[3];
  s[3]=s[4];
  s[4]=t;

  return v;
}
#endif

static ucell *aligncell(ucell *v)
{
  if (pc_cellsize==2)
    return align16(v);
  if (pc_cellsize==4)
    return align32(v);
# if PAWN_CELL_SIZE>=64
    if (pc_cellsize==8)
      return align64(v);
# endif
  assert(0);
}

#else
# define align16(v)    (v)
# define align32(v)    (v)
# define align64(v)    (v)
# define aligncell(v)  (v)
#endif

static char *stripcomment(char *str)
{
  char *ptr=strchr(str,';');
  if (ptr!=NULL) {
    *ptr++='\n';        /* terminate the line, but leave the '\n' */
    *ptr='\0';
  } /* if */
  return str;
}

static bool write_cell(FILE *fbin,ucell c)
{
  if (!fbin)
    return false;
  assert((pc_lengthbin(fbin) % pc_cellsize) == 0);
# if defined PAWN_KEELOQ
    if (pc_cryptkey!=0) {
      uint32_t *ptr=(uint32_t*)&c;
      assert(pc_cellsize==4 || pc_cellsize==8);
      *ptr=KeeLoq_Encrypt(*ptr,pc_cryptkey);
      if (pc_cellsize==8) {
        ptr++;
        *ptr=KeeLoq_Encrypt(*ptr,pc_cryptkey);
      } /* if */
    } /* if */
# endif
  writeerror |= !pc_writebin(fbin,aligncell(&c),pc_cellsize);
  return true;
}

static cell noop(FILE *fbin,const char *params,cell opcode,cell cip)
{
  (void)fbin;
  (void)params;
  (void)opcode;
  (void)cip;
  return 0;
}

static cell set_curfile(FILE *fbin,const char *params,cell opcode,cell cip)
{
  (void)fbin;
  (void)opcode;
  (void)cip;
  fcurrent=(short)getparamvalue(params,NULL);
  return 0;
}

static cell parm0(FILE *fbin,const char *params,cell opcode,cell cip)
{
  (void)params;
  (void)cip;
  write_cell(fbin,opcode);
  return opcodes(1);
}

static cell parm1(FILE *fbin,const char *params,cell opcode,cell cip)
{
  ucell p=getparamvalue(params,NULL);
  (void)cip;
  write_cell(fbin,opcode);
  write_cell(fbin,p);
  return opcodes(1)+opargs(1);
}

static cell parm1_p(FILE *fbin,const char *params,cell opcode,cell cip)
{
  ucell p=getparamvalue(params,NULL);
  (void)cip;
  assert(p<((ucell)1<<(pc_cellsize*4)));
  assert(opcode>=0 && opcode<=255);
  p=(p<<pc_cellsize*4) | opcode;
  write_cell(fbin,p);
  return opcodes(1);
}

static cell parm2(FILE *fbin,const char *params,cell opcode,cell cip)
{
  ucell p1=getparamvalue(params,&params);
  ucell p2=getparamvalue(params,NULL);
  (void)cip;
  write_cell(fbin,opcode);
  write_cell(fbin,p1);
  write_cell(fbin,p2);
  return opcodes(1)+opargs(2);
}

static cell parmx(FILE *fbin,const char *params,cell opcode,cell cip)
{
  unsigned idx;
  ucell count=getparamvalue(params,&params);
  (void)cip;
  write_cell(fbin,opcode);
  write_cell(fbin,count);
  for (idx=0; idx<count; idx++) {
    ucell p=getparamvalue(params,&params);
    write_cell(fbin,p);
  } /* for */
  return opcodes(1)+opargs(count+1);
}

static cell parmx_p(FILE *fbin,const char *params,cell opcode,cell cip)
{
  unsigned idx;
  ucell p;
  ucell count=getparamvalue(params,&params);
  (void)cip;
  assert(count<((ucell)1<<(pc_cellsize*4)));
  assert(opcode>=0 && opcode<=255);
  p=(count<<pc_cellsize*4) | opcode;
  write_cell(fbin,p);
  for (idx=0; idx<count; idx++) {
    p=getparamvalue(params,&params);
    write_cell(fbin,p);
  } /* for */
  return opcodes(1)+opargs(count);
}

static cell do_dump(FILE *fbin,const char *params,cell opcode,cell cip)
{
  int num = 0;

  (void)opcode;
  (void)cip;
  while (*params!='\0') {
    ucell p=getparamvalue(params,&params);
    write_cell(fbin,p);
    num++;
    while (isspace(*params))
      params++;
  } /* while */
  return num*pc_cellsize;
}

static cell do_call(FILE *fbin,const char *params,cell opcode,cell cip)
{
  char name[sNAMEMAX+1];
  int i;
  for (i=0; !isspace(*params); i++,params++) {
    assert(*params!='\0');
    assert(i<sNAMEMAX);
    name[i]=*params;
  } /* for */
  name[i]='\0';

  ucell p=0;
  if (name[0]=='l' && name[1]=='.') {
    /* this is a label, not a function symbol */
    i=(int)hex2ucell(name+2,NULL);
    assert(i>=0 && i<sc_labnum);
    if (lbltab!=NULL)
      p=lbltab[i]-cip;          /* make relative address */
  } else {
    /* look up the function address; note that the correct file number must
     * already have been set (in order for static globals to be found).
     */
    const symbol *sym=findglb(name,sGLOBAL);
    assert(sym!=NULL);
    assert(sym->ident==iFUNCTN);
    assert(sym->scope==sGLOBAL);
    p=sym->addr-cip;            /* make relative address */
  } /* if */

  write_cell(fbin,opcode);
  write_cell(fbin,p);
  return opcodes(1)+opargs(1);
}

static cell do_jump(FILE *fbin,const char *params,cell opcode,cell cip)
{
  int i=(int)hex2ucell(params,NULL);
  assert(i>=0 && i<sc_labnum);
  assert(lbltab!=NULL);
  ucell p=lbltab[i]-cip;            /* make relative address */
  write_cell(fbin,opcode);
  write_cell(fbin,p);
  return opcodes(1)+opargs(1);
}

static cell do_switch(FILE *fbin,const char *params,cell opcode,cell cip)
{
  int i=(int)hex2ucell(params,NULL);
  assert(i>=0 && i<sc_labnum);
  assert(lbltab!=NULL);
  ucell p=lbltab[i]-cip;
  write_cell(fbin,opcode);
  write_cell(fbin,p);
  return opcodes(1)+opargs(1);
}

static cell do_case(FILE *fbin,const char *params,cell opcode,cell cip)
{
  (void)opcode;
  ucell v=hex2ucell(params,&params);
  int i=(int)hex2ucell(params,NULL);
  assert(i>=0 && i<sc_labnum);
  assert(lbltab!=NULL);
  ucell p=lbltab[i]-cip;
  write_cell(fbin,v);
  write_cell(fbin,p);
  return opcodes(0)+opargs(2);
}

static cell do_caseovl(FILE *fbin,const char *params,cell opcode,cell cip)
{
  ucell v=hex2ucell(params,&params);
  ucell p=hex2ucell(params,NULL);
  (void)opcode;
  (void)cip;
  write_cell(fbin,v);
  write_cell(fbin,p);
  return opcodes(0)+opargs(2);
}

static OPCODE opcodelist[] = {
  /* opcodes in alphapetically sorted order */
  {  0, "CODE",        sIN_CSEG, set_curfile, 1, 0 },
  {  0, "DATA",        sIN_DSEG, set_curfile, 1, 0 },
  {  0, "STKSIZE",     0,        noop,        1, 0 },
  { 44, "add",         sIN_CSEG, parm0,       1, 0 },
  {100, "add.c",       sIN_CSEG, parm1,       2, 0 },
  {160, "add.p.c",     sIN_CSEG, parm1_p,     3, 0 },
  { 12, "addr.alt",    sIN_CSEG, parm1,       1, SEQPOINT(sALT) },
  {134, "addr.p.alt",  sIN_CSEG, parm1_p,     3, SEQPOINT(sALT) },
  {133, "addr.p.pri",  sIN_CSEG, parm1_p,     3, SEQPOINT(sPRI) },
  { 11, "addr.pri",    sIN_CSEG, parm1,       1, SEQPOINT(sPRI) },
  {141, "align.p.pri", sIN_CSEG, parm1_p,     3, 0 },
  { 18, "align.pri",   sIN_CSEG, parm1,       1, 0 },
  { 46, "and",         sIN_CSEG, parm0,       1, 0 },
  { 68, "bounds",      sIN_CSEG, parm1,       1, 0 },
  {174, "bounds.p",    sIN_CSEG, parm1_p,     3, 0 },
  { 73, "break",       sIN_CSEG, parm0,       1, 0 },
  { 33, "call",        sIN_CSEG, do_call,     1, 0 },
  { 77, "call.ovl",    sIN_CSEG, parm1,       1, 0 },
  {  0, "case",        sIN_CSEG, do_case,     1, 0 },
  {  0, "case.ovl",    sIN_CSEG, do_caseovl,  1, 0 },
  { 74, "casetbl",     sIN_CSEG, parm0,       1, 0 },
  { 80, "casetbl.ovl", sIN_CSEG, parm0,       1, 0 },
  { 65, "cmps",        sIN_CSEG, parm1,       1, 0 },
  {171, "cmps.p",      sIN_CSEG, parm1_p,     3, 0 },
  {122, "const",       sIN_CSEG, parm2,       2, 0 },
  { 10, "const.alt",   sIN_CSEG, parm1,       1, SEQPOINT(sALT) },
  {132, "const.p.alt", sIN_CSEG, parm1_p,     3, SEQPOINT(sALT) },
  {131, "const.p.pri", sIN_CSEG, parm1_p,     3, SEQPOINT(sPRI) },
  {  9, "const.pri",   sIN_CSEG, parm1,       1, SEQPOINT(sPRI) },
  {123, "const.s",     sIN_CSEG, parm2,       2, 0 },
  {110, "dec",         sIN_CSEG, parm1,       2, 0 },
  { 62, "dec.alt",     sIN_CSEG, parm0,       1, 0 },
  { 63, "dec.i",       sIN_CSEG, parm0,       1, 0 },
  {168, "dec.p",       sIN_CSEG, parm1_p,     3, 0 },
  {169, "dec.p.s",     sIN_CSEG, parm1_p,     3, 0 },
  { 61, "dec.pri",     sIN_CSEG, parm0,       1, 0 },
  {111, "dec.s",       sIN_CSEG, parm1,       2, 0 },
  {  0, "dump",        sIN_DSEG, do_dump,     1, 0 },
  { 52, "eq",          sIN_CSEG, parm0,       1, 0 },
  {107, "eq.c.alt",    sIN_CSEG, parm1,       2, SEQPOINT(sPRI) },
  {106, "eq.c.pri",    sIN_CSEG, parm1,       2, 0 },
  {165, "eq.p.c.alt",  sIN_CSEG, parm1_p,     3, SEQPOINT(sPRI) },
  {164, "eq.p.c.pri",  sIN_CSEG, parm1_p,     3, 0 },
  { 66, "fill",        sIN_CSEG, parm1,       1, 0 },
  {172, "fill.p",      sIN_CSEG, parm1_p,     3, 0 },
  { 67, "halt",        sIN_CSEG, parm1,       1, 0 },
  {173, "halt.p",      sIN_CSEG, parm1_p,     3, 0 },
  { 29, "heap",        sIN_CSEG, parm1,       1, SEQPOINT(sALT) },
  {157, "heap.p",      sIN_CSEG, parm1_p,     3, SEQPOINT(sALT) },
  { 83, "idxaddr",     sIN_CSEG, parm0,       2, 0 },
  { 84, "idxaddr.b",   sIN_CSEG, parm1,       2, 0 },
  {140, "idxaddr.p.b", sIN_CSEG, parm1_p,     3, 0 },
  {108, "inc",         sIN_CSEG, parm1,       2, 0 },
  { 59, "inc.alt",     sIN_CSEG, parm0,       1, 0 },
  { 60, "inc.i",       sIN_CSEG, parm0,       1, 0 },
  {166, "inc.p",       sIN_CSEG, parm1_p,     3, 0 },
  {167, "inc.p.s",     sIN_CSEG, parm1_p,     3, 0 },
  { 58, "inc.pri",     sIN_CSEG, parm0,       1, 0 },
  {109, "inc.s",       sIN_CSEG, parm1,       2, 0 },
  { 51, "invert",      sIN_CSEG, parm0,       1, 0 },
  { 92, "jeq",         sIN_CSEG, do_jump,     2, 0 },
  { 93, "jneq",        sIN_CSEG, do_jump,     2, 0 },
  { 36, "jnz",         sIN_CSEG, do_jump,     1, 0 },
  { 97, "jsgeq",       sIN_CSEG, do_jump,     2, 0 },
  { 96, "jsgrtr",      sIN_CSEG, do_jump,     2, 0 },
  { 95, "jsleq",       sIN_CSEG, do_jump,     2, 0 },
  { 94, "jsless",      sIN_CSEG, do_jump,     2, 0 },
  { 34, "jump",        sIN_CSEG, do_jump,     1, 0 },
  { 35, "jzer",        sIN_CSEG, do_jump,     1, 0 },
  { 19, "lctrl",       sIN_CSEG, parm1,       1, SEQPOINT(sPRI) },
  { 81, "lidx",        sIN_CSEG, parm0,       2, 0 },
  { 82, "lidx.b",      sIN_CSEG, parm1,       2, 0 },
  {139, "lidx.p.b",    sIN_CSEG, parm1_p,     3, 0 },
  {  2, "load.alt",    sIN_CSEG, parm1,       1, SEQPOINT(sALT) },
  {  7, "load.i",      sIN_CSEG, parm0,       1, 0 },
  {125, "load.p.alt",  sIN_CSEG, parm1_p,     3, SEQPOINT(sALT) },
  {124, "load.p.pri",  sIN_CSEG, parm1_p,     3, SEQPOINT(sPRI) },
  {127, "load.p.s.alt",sIN_CSEG, parm1_p,     3, SEQPOINT(sALT) },
  {126, "load.p.s.pri",sIN_CSEG, parm1_p,     3, SEQPOINT(sPRI) },
  {  1, "load.pri",    sIN_CSEG, parm1,       1, SEQPOINT(sPRI) },
  {  4, "load.s.alt",  sIN_CSEG, parm1,       1, SEQPOINT(sALT) },
  {  3, "load.s.pri",  sIN_CSEG, parm1,       1, SEQPOINT(sPRI) },
  {120, "load2",       sIN_CSEG, parm2,       2, SEQPOINT(sPRI) | SEQPOINT(sALT) },
  {121, "load2.s",     sIN_CSEG, parm2,       2, SEQPOINT(sPRI) | SEQPOINT(sALT) },
  {  8, "lodb.i",      sIN_CSEG, parm1,       1, 0 },
  {130, "lodb.p.i",    sIN_CSEG, parm1_p,     3, 0 },
  {129, "lref.p.s.alt",sIN_CSEG, parm1_p,     3, SEQPOINT(sALT) },
  {128, "lref.p.s.pri",sIN_CSEG, parm1_p,     3, SEQPOINT(sPRI) },
  {  6, "lref.s.alt",  sIN_CSEG, parm1,       1, SEQPOINT(sALT) },
  {  5, "lref.s.pri",  sIN_CSEG, parm1,       1, SEQPOINT(sPRI) },
  { 64, "movs",        sIN_CSEG, parm1,       1, 0 },
  {170, "movs.p",      sIN_CSEG, parm1_p,     3, 0 },
  { 50, "neg",         sIN_CSEG, parm0,       1, 0 },
  { 53, "neq",         sIN_CSEG, parm0,       1, 0 },
  {  0, "nop",         sIN_CSEG, parm0,       1, 0 },
  { 49, "not",         sIN_CSEG, parm0,       1, 0 },
  { 47, "or",          sIN_CSEG, parm0,       1, 0 },
  { 27, "pick",        sIN_CSEG, parm1,       1, SEQPOINT(sPRI) },
  { 26, "pop.alt",     sIN_CSEG, parm0,       1, SEQPOINT(sALT) },
  { 25, "pop.pri",     sIN_CSEG, parm0,       1, SEQPOINT(sPRI) },
  { 30, "proc",        sIN_CSEG, parm0,       1, 0 },
  { 86, "push",        sIN_CSEG, parm1,       2, 0 },
  { 88, "push.adr",    sIN_CSEG, parm1,       2, 0 },
  { 23, "push.alt",    sIN_CSEG, parm0,       1, 0 },
  { 85, "push.c",      sIN_CSEG, parm1,       2, 0 },
  {143, "push.p",      sIN_CSEG, parm1_p,     3, 0 },
  {145, "push.p.adr",  sIN_CSEG, parm1_p,     3, 0 },
  {142, "push.p.c",    sIN_CSEG, parm1_p,     3, 0 },
  {144, "push.p.s",    sIN_CSEG, parm1_p,     3, 0 },
  { 22, "push.pri",    sIN_CSEG, parm0,       1, 0 },
  { 87, "push.s",      sIN_CSEG, parm1,       2, 0 },
  {114, "pushm",       sIN_CSEG, parmx,       2, 0 },
  {116, "pushm.adr",   sIN_CSEG, parmx,       2, 0 },
  {113, "pushm.c",     sIN_CSEG, parmx,       2, 0 },
  {150, "pushm.p",     sIN_CSEG, parmx_p,     3, 0 },
  {152, "pushm.p.adr", sIN_CSEG, parmx_p,     3, 0 },
  {149, "pushm.p.c",   sIN_CSEG, parmx_p,     3, 0 },
  {151, "pushm.p.s",   sIN_CSEG, parmx_p,     3, 0 },
  {115, "pushm.s",     sIN_CSEG, parmx,       2, 0 },
  { 91, "pushr.adr",   sIN_CSEG, parm1,       2, 0 },
  { 89, "pushr.c",     sIN_CSEG, parm1,       2, 0 },
  {148, "pushr.p.adr", sIN_CSEG, parm1_p,     3, 0 },
  {146, "pushr.p.c",   sIN_CSEG, parm1_p,     3, 0 },
  {147, "pushr.p.s",   sIN_CSEG, parm1_p,     3, 0 },
  { 24, "pushr.pri",   sIN_CSEG, parm0,       1, 0 },
  { 90, "pushr.s",     sIN_CSEG, parm1,       2, 0 },
  {119, "pushrm.adr",  sIN_CSEG, parmx,       2, 0 },
  {117, "pushrm.c",    sIN_CSEG, parmx,       2, 0 },
  {155, "pushrm.p.adr",sIN_CSEG, parmx_p,     3, 0 },
  {153, "pushrm.p.c",  sIN_CSEG, parmx_p,     3, 0 },
  {154, "pushrm.p.s",  sIN_CSEG, parmx_p,     3, 0 },
  {118, "pushrm.s",    sIN_CSEG, parmx,       2, 0 },
  { 31, "ret",         sIN_CSEG, parm0,       1, 0 },
  { 32, "retn",        sIN_CSEG, parm0,       1, 0 },
  { 78, "retn.ovl",    sIN_CSEG, parm0,       1, 0 },
  { 20, "sctrl",       sIN_CSEG, parm1,       1, 0 },
  { 43, "sdiv",        sIN_CSEG, parm0,       1, 0 },
  { 98, "sdiv.inv",    sIN_CSEG, parm0,       2, 0 },
  { 57, "sgeq",        sIN_CSEG, parm0,       1, 0 },
  { 56, "sgrtr",       sIN_CSEG, parm0,       1, 0 },
  { 37, "shl",         sIN_CSEG, parm0,       1, 0 },
  { 41, "shl.c.alt",   sIN_CSEG, parm1,       1, 0 },
  { 40, "shl.c.pri",   sIN_CSEG, parm1,       1, 0 },
  {159, "shl.p.c.alt", sIN_CSEG, parm1_p,     3, 0 },
  {158, "shl.p.c.pri", sIN_CSEG, parm1_p,     3, 0 },
  { 38, "shr",         sIN_CSEG, parm0,       1, 0 },
  { 55, "sleq",        sIN_CSEG, parm0,       1, 0 },
  { 54, "sless",       sIN_CSEG, parm0,       1, 0 },
  { 42, "smul",        sIN_CSEG, parm0,       1, 0 },
  {101, "smul.c",      sIN_CSEG, parm1,       2, 0 },
  {161, "smul.p.c",    sIN_CSEG, parm1_p,     3, 0 },
  {137, "sref.p.s",    sIN_CSEG, parm1_p,     3, 0 },
  { 15, "sref.s",      sIN_CSEG, parm1,       1, 0 },
  { 39, "sshr",        sIN_CSEG, parm0,       1, 0 },
  { 28, "stack",       sIN_CSEG, parm1,       1, SEQPOINT(sALT) },
  {156, "stack.p",     sIN_CSEG, parm1_p,     3, SEQPOINT(sALT) },
  { 13, "stor",        sIN_CSEG, parm1,       1, 0 },
  { 16, "stor.i",      sIN_CSEG, parm0,       1, 0 },
  {135, "stor.p",      sIN_CSEG, parm1_p,     3, 0 },
  {136, "stor.p.s",    sIN_CSEG, parm1_p,     3, 0 },
  { 14, "stor.s",      sIN_CSEG, parm1,       1, 0 },
  { 17, "strb.i",      sIN_CSEG, parm1,       1, 0 },
  {138, "strb.p.i",    sIN_CSEG, parm1_p,     3, 0 },
  { 45, "sub",         sIN_CSEG, parm0,       1, 0 },
  { 99, "sub.inv",     sIN_CSEG, parm0,       2, 0 },
  { 72, "swap.alt",    sIN_CSEG, parm0,       1, 0 },
  { 71, "swap.pri",    sIN_CSEG, parm0,       1, 0 },
  { 70, "switch",      sIN_CSEG, do_switch,   1, 0 },
  { 79, "switch.ovl",  sIN_CSEG, do_switch,   1, 0 },
  { 69, "sysreq",      sIN_CSEG, parm1,       1, 0 },
/*{ 75, "sysreq.d",    sIN_CSEG, parm1,       1, 0 }, not generated by the compiler */
  {112, "sysreq.n",    sIN_CSEG, parm2,       2, 0 },
/*{ 76, "sysreq.nd",   sIN_CSEG, parm2,       1, 0 }, not generated by the compiler */
  { 21, "xchg",        sIN_CSEG, parm0,       1, 0 },
  { 48, "xor",         sIN_CSEG, parm0,       1, 0 },
  {104, "zero",        sIN_CSEG, parm1,       2, 0 },
  {103, "zero.alt",    sIN_CSEG, parm0,       2, SEQPOINT(sALT) },
  {162, "zero.p",      sIN_CSEG, parm1_p,     3, 0 },
  {163, "zero.p.s",    sIN_CSEG, parm1_p,     3, 0 },
  {102, "zero.pri",    sIN_CSEG, parm0,       2, SEQPOINT(sPRI) },
  {105, "zero.s",      sIN_CSEG, parm1,       2, 0 },
};

#define MAX_INSTR_LEN   30
static unsigned findopcode(const char *instr,int instr_len)
{
  assert(instr_len<MAX_INSTR_LEN);
  char str[MAX_INSTR_LEN];
  strlcpy(str,instr,MAX_INSTR_LEN);
  if (instr_len<MAX_INSTR_LEN)
    str[instr_len]='\0';

  /* look up the instruction with a binary search; this look-up is case sensitive */
  unsigned low=0;
  unsigned high=countof(opcodelist)-1;
  while (low<high) {
    int mid=(low+high)/2;
    assert(opcodelist[mid].name!=NULL);
    int cmp=strcmp(str,opcodelist[mid].name);
    if (cmp>0)
      low=mid+1;
    else
      high=mid;
  } /* while */

  assert(low==high);
  assert(strcmp(str,opcodelist[low].name)==0);
  return low;
}

SC_FUNC int assemble(FILE *fout,FILE *fin)
{
  AMX_HEADER hdr;
  AMX_FUNCSTUB func;
  unsigned numpublics,numnatives,numoverlays,numlibraries,numpubvars,numtags;
  int padding;
  long nametablesize,nameofs;
  char line[512];
  char *instr,*params;
  int pass,size;
  uint16_t count;
  symbol *sym;
  symbol **nativelist;
  constvalue *constptr;
  cell mainaddr;
  char nullchar;

# if !defined NDEBUG
    /* verify that the opcode list is sorted */
    {
#     define MAX_OPCODE 176
      unsigned char opcodearray[MAX_OPCODE+1];
      assert(opcodelist[1].name!=NULL);
      memset(opcodearray,0,sizeof opcodearray);
      for (unsigned i=1; i<countof(opcodelist); i++) {
        assert(opcodelist[i].name!=NULL);
        assert(strcmp(opcodelist[i].name,opcodelist[i-1].name)>0);
        /* also verify that no opcode number appears twice */
        assert((int)opcodelist[i].opcode<=MAX_OPCODE);
        assert(opcodelist[i].opcode==0 || opcodearray[(int)opcodelist[i].opcode]==0);
        opcodearray[(int)opcodelist[i].opcode] += 1;
      } /* for */
    }
# endif

  writeerror=false;
  nametablesize=sizeof(int16_t);
  numpublics=0;
  numnatives=0;
  numpubvars=0;
  numoverlays=0;
  mainaddr=-1;
  /* count number of public and native functions and public variables */
  for (sym=glbtab.next; sym!=NULL; sym=sym->next) {
    int match=0;
    if (sym->ident==iFUNCTN) {
      if ((sym->usage & uNATIVE)!=0 && (sym->usage & uREAD)!=0 && sym->index>=0)
        match=++numnatives;
      if ((sym->usage & uPUBLIC)!=0 && (sym->usage & uDEFINE)!=0)
        match=++numpublics;
      if (pc_overlays>0 && (sym->usage & uNATIVE)==0
          && (sym->usage & (uREAD | uPUBLIC))!=0 && (sym->usage & uDEFINE)!=0)
      {
        if (strcmp(sym->name,_ENTRYFUNC)!=0)
          ++numoverlays;  /* there is no stub function for state entry functions */
        if (sym->states!=NULL) {
          /* for functions with states, write an overlay block for every implementation */
          statelist *stlist;
          for (stlist=sym->states->next; stlist!=NULL; stlist=stlist->next)
            ++numoverlays;
        } /* if */
      } /* if */
      if ((sym->flags & flgENTRYPOINT)!=0) {
        assert(sym->scope==sGLOBAL);
        mainaddr=(pc_overlays>0) ? sym->index : sym->addr;
      } /* if */
    } else if (sym->ident==iVARIABLE) {
      if ((sym->usage & uPUBLIC)!=0 && (sym->usage & (uREAD | uWRITTEN))!=0)
        match=++numpubvars;
    } /* if */
    if (match) {
      char alias[sNAMEMAX+1];
      assert(sym!=NULL);
      if ((sym->usage & uNATIVE)==0 || !lookup_alias(alias,sym->name)) {
        assert(strlen(sym->name)<=sNAMEMAX);
        strcpy(alias,sym->name);
      } /* if */
      nametablesize+=(int)strlen(alias)+1;
    } /* if */
  } /* for */
  assert(numnatives==ntv_funcid);

  /* count number of libraries */
  numlibraries=0;
  if (pc_addlibtable) {
    for (constptr=libname_tab.next; constptr!=NULL; constptr=constptr->next) {
      if (constptr->value>0) {
        assert(strlen(constptr->name)>0);
        numlibraries++;
        nametablesize+=(int)strlen(constptr->name)+1;
      } /* if */
    } /* for */
  } /* if */

  /* count number of public tags */
  numtags=0;
  for (constptr=tagname_tab.next; constptr!=NULL; constptr=constptr->next) {
    if ((constptr->value & PUBLICTAG)!=0) {
      assert(strlen(constptr->name)>0);
      numtags++;
      nametablesize+=(int)strlen(constptr->name)+1;
    } /* if */
  } /* for */

  /* adjust the number of overlays by the special overlays */
  if (pc_overlays>0)
    for (unsigned i=0; i<ovlFIRST; i++)
      if (pc_ovl0size[i][1]!=0)
        numoverlays++;

  /* pad the header to sc_dataalign
   * => thereby the code segment is aligned
   * => since the code segment is padded to a sc_dataalign boundary, the data segment is aligned
   * => and thereby the stack top is aligned too
   */
  assert(pc_dataalign!=0);
  padding= (int)(pc_dataalign - (sizeof hdr + nametablesize) % pc_dataalign);
  if (padding==pc_dataalign)
    padding=0;

  /* write the abstract machine header */
  memset(&hdr, 0, sizeof hdr);
  if (pc_cellsize==2)
    hdr.magic=(unsigned short)AMX_MAGIC_16;
  else if (pc_cellsize==4)
    hdr.magic=(unsigned short)AMX_MAGIC_32;
  else if (pc_cellsize==8)
    hdr.magic=(unsigned short)AMX_MAGIC_64;
  hdr.file_version=CUR_FILE_VERSION;
  hdr.amx_version=MIN_AMX_VERSION;
  hdr.flags=(short)(sc_debug & sSYMBOLIC);
  if (sc_debug==0)
    hdr.flags|=AMX_FLAG_NOCHECKS;
  if (pc_memflags & suSLEEP_INSTR)
    hdr.flags|=AMX_FLAG_SLEEP;
  if (pc_overlays>0)
    hdr.flags|=AMX_FLAG_OVERLAY;
  if (pc_cryptkey!=0)
    hdr.flags|=AMX_FLAG_CRYPT;
  hdr.defsize=sizeof(AMX_FUNCSTUB);
  hdr.publics=sizeof hdr; /* public table starts right after the header */
  hdr.natives=hdr.publics + numpublics*sizeof(AMX_FUNCSTUB);
  hdr.libraries=hdr.natives + numnatives*sizeof(AMX_FUNCSTUB);
  hdr.pubvars=hdr.libraries + numlibraries*sizeof(AMX_FUNCSTUB);
  hdr.tags=hdr.pubvars + numpubvars*sizeof(AMX_FUNCSTUB);
  hdr.overlays=hdr.tags + numtags*sizeof(AMX_FUNCSTUB);
  hdr.nametable=hdr.overlays + numoverlays*sizeof(AMX_OVERLAYINFO);
  hdr.cod=hdr.nametable + nametablesize + padding;
  hdr.dat=(int32_t)(hdr.cod + code_idx);
  hdr.hea=(int32_t)(hdr.dat + glb_declared*pc_cellsize);
  hdr.stp=(int32_t)(hdr.hea + pc_stksize*pc_cellsize);
  hdr.cip=(int32_t)(mainaddr);
  hdr.size=hdr.hea;
  pc_writebin(fout,&hdr,sizeof hdr);

  /* dump zeros up to the rest of the header, so that we can easily "seek" */
  nullchar='\0';
  for (nameofs=sizeof hdr; nameofs<hdr.cod; nameofs++)
    pc_writebin(fout,&nullchar,1);
  nameofs=hdr.nametable+sizeof(int16_t);

  /* write the public functions table */
  count=0;
  for (sym=glbtab.next; sym!=NULL; sym=sym->next) {
    if (sym->ident==iFUNCTN
        && (sym->usage & uPUBLIC)!=0 && (sym->usage & uDEFINE)!=0)
    {
      assert(sym->scope==sGLOBAL);
      /* in the case of overlays, write the overlay index rather than the address */
      func.address=(uint32_t)((pc_overlays>0) ? sym->index : sym->addr);
      func.nameofs=nameofs;
#     if BYTE_ORDER==BIG_ENDIAN
        align32(&func.address);
        align32(&func.nameofs);
#     endif
      pc_resetbin(fout,hdr.publics+count*sizeof(AMX_FUNCSTUB));
      pc_writebin(fout,&func,sizeof func);
      pc_resetbin(fout,nameofs);
      pc_writebin(fout,sym->name,(int)strlen(sym->name)+1);
      nameofs+=(int)strlen(sym->name)+1;
      count++;
    } /* if */
  } /* for */

  /* write the natives table */
  /* The native functions must be written in sorted order. (They are
   * sorted on their "id", not on their name). A nested loop to find
   * each successive function would be an O(n^2) operation. But we
   * do not really need to sort, because the native function id's
   * are sequential and there are no duplicates. So we first walk
   * through the complete symbol list and store a pointer to every
   * native function of interest in a temporary table, where its id
   * serves as the index in the table. Now we can walk the table and
   * have all native functions in sorted order.
   */
  if (numnatives>0) {
    nativelist=(symbol **)malloc(numnatives*sizeof(symbol *));
    if (nativelist==NULL)
      error(103);               /* insufficient memory */
#   if !defined NDEBUG
      memset(nativelist,0,numnatives*sizeof(symbol *)); /* for NULL checking */
#   endif
    for (sym=glbtab.next; sym!=NULL; sym=sym->next) {
      if (sym->ident==iFUNCTN && (sym->usage & uNATIVE)!=0 && (sym->usage & uREAD)!=0 && sym->index>=0) {
        assert(sym->index<(int)numnatives);
        nativelist[sym->index]=sym;
      } /* if */
    } /* for */
    count=0;
    for (unsigned i=0; i<numnatives; i++) {
      char alias[sNAMEMAX+1];
      sym=nativelist[i];
      assert(sym!=NULL);
      if (!lookup_alias(alias,sym->name)) {
        assert(strlen(sym->name)<=sNAMEMAX);
        strcpy(alias,sym->name);
      } /* if */
      assert(sym->scope==sGLOBAL);
      func.address=0;
      func.nameofs=nameofs;
#     if BYTE_ORDER==BIG_ENDIAN
        align32(&func.address);
        align32(&func.nameofs);
#     endif
      pc_resetbin(fout,hdr.natives+count*sizeof(AMX_FUNCSTUB));
      pc_writebin(fout,&func,sizeof func);
      pc_resetbin(fout,nameofs);
      pc_writebin(fout,alias,(int)strlen(alias)+1);
      nameofs+=(int)strlen(alias)+1;
      count++;
    } /* for */
    free(nativelist);
  } /* if */

  /* write the libraries table */
  if (pc_addlibtable) {
    count=0;
    for (constptr=libname_tab.next; constptr!=NULL; constptr=constptr->next) {
      if (constptr->value>0) {
        assert(strlen(constptr->name)>0);
        func.address=0;
        func.nameofs=nameofs;
#       if BYTE_ORDER==BIG_ENDIAN
          align32(&func.address);
          align32(&func.nameofs);
#       endif
        pc_resetbin(fout,hdr.libraries+count*sizeof(AMX_FUNCSTUB));
        pc_writebin(fout,&func,sizeof func);
        pc_resetbin(fout,nameofs);
        pc_writebin(fout,constptr->name,(int)strlen(constptr->name)+1);
        nameofs+=(int)strlen(constptr->name)+1;
        count++;
      } /* if */
    } /* for */
  } /* if */

  /* write the public variables table */
  count=0;
  for (sym=glbtab.next; sym!=NULL; sym=sym->next) {
    if (sym->ident==iVARIABLE && (sym->usage & uPUBLIC)!=0 && (sym->usage & (uREAD | uWRITTEN))!=0) {
      assert((sym->usage & uDEFINE)!=0);
      assert(sym->scope==sGLOBAL);
      func.address=(uint32_t)sym->addr;
      func.nameofs=nameofs;
#     if BYTE_ORDER==BIG_ENDIAN
        align32(&func.address);
        align32(&func.nameofs);
#     endif
      pc_resetbin(fout,hdr.pubvars+count*sizeof(AMX_FUNCSTUB));
      pc_writebin(fout,&func,sizeof func);
      pc_resetbin(fout,nameofs);
      pc_writebin(fout,sym->name,(int)strlen(sym->name)+1);
      nameofs+=(int)strlen(sym->name)+1;
      count++;
    } /* if */
  } /* for */

  /* write the public tagnames table */
  count=0;
  for (constptr=tagname_tab.next; constptr!=NULL; constptr=constptr->next) {
    if ((constptr->value & PUBLICTAG)!=0) {
      assert(strlen(constptr->name)>0);
      func.address=(uint32_t)(constptr->value & TAGMASK);
      func.nameofs=nameofs;
#     if BYTE_ORDER==BIG_ENDIAN
        align32(&func.address);
        align32(&func.nameofs);
#     endif
      pc_resetbin(fout,hdr.tags+count*sizeof(AMX_FUNCSTUB));
      pc_writebin(fout,&func,sizeof func);
      pc_resetbin(fout,nameofs);
      pc_writebin(fout,constptr->name,(int)strlen(constptr->name)+1);
      nameofs+=(int)strlen(constptr->name)+1;
      count++;
    } /* if */
  } /* for */

  /* write the "maximum name length" field in the name table */
  assert(nameofs==hdr.nametable+nametablesize);
  pc_resetbin(fout,hdr.nametable);
  count=sNAMEMAX;
# if BYTE_ORDER==BIG_ENDIAN
    align16(&count);
# endif
  pc_writebin(fout,&count,sizeof count);

  /* write the overlay table */
  if (pc_overlays>0) {
    AMX_OVERLAYINFO info;
#   if !defined NDEBUG
      int entrycount=0;
#   endif
    pc_resetbin(fout,hdr.overlays);
    /* first the special overlay(s) for the return point(s) */
    for (int i=0; i<ovlFIRST; i++) {
      if (pc_ovl0size[i][1]!=0) {
        info.offset=pc_ovl0size[i][0];
        info.size=pc_ovl0size[i][1];
#       if BYTE_ORDER==BIG_ENDIAN
          align32(&info.offset);
          align32(&info.size);
#       endif
        pc_writebin(fout,&info,sizeof info);
#       if !defined NDEBUG
          entrycount++;
#       endif
      } /* if */
    } /* for */
    /* now all real overlay functions */
    for (sym=glbtab.next; sym!=NULL; sym=sym->next) {
      if (sym->ident==iFUNCTN
          && (sym->usage & uNATIVE)==0 && (sym->usage & (uREAD | uPUBLIC))!=0
          && (sym->usage & uDEFINE)!=0)
      {
        assert(sym->scope==sGLOBAL);
        assert(strcmp(sym->name,_ENTRYFUNC)==0 || sym->index==entrycount++);/* overlay indices must be in sequential order */
        assert(strcmp(sym->name,_ENTRYFUNC)==0 || sym->addr<sym->codeaddr);
        /* write the overlay for the stub function first */
        if (strcmp(sym->name,_ENTRYFUNC)!=0) {
          /* there is no stub function for state entry functions */
          info.offset=(int32_t)sym->addr;
          info.size=(uint32_t)(sym->codeaddr - sym->addr);
#         if BYTE_ORDER==BIG_ENDIAN
            align32(&info.offset);
            align32(&info.size);
#         endif
          pc_writebin(fout,&info,sizeof info);
        } /* if */
        if (sym->states!=NULL) {
          /* for functions with states, write an overlay block for every implementation */
          statelist *stlist;
          for (stlist=sym->states->next; stlist!=NULL; stlist=stlist->next) {
            assert(stlist->label==entrycount++);
            info.offset=(int32_t)stlist->addr;
            info.size=(int32_t)(stlist->endaddr - stlist->addr);
#           if BYTE_ORDER==BIG_ENDIAN
              align32(&info.offset);
              align32(&info.size);
#           endif
            pc_writebin(fout,&info,sizeof info);
          } /* for */
        } /* if */
      } /* if */
    } /* for */
  } /* if */
  pc_resetbin(fout,hdr.cod);

  /* First pass: relocate all labels */
  /* This pass is necessary because the code addresses of labels is only known
   * after the peephole optimization flag. Labels can occur inside expressions
   * (e.g. the conditional operator), which are optimized.
   */
  lbltab=NULL;
  if (sc_labnum>0) {
    cell codeindex=0; /* address of the current opcode similar to "code_idx" */
    /* only very short programs have zero labels; no first pass is needed
     * if there are no labels */
    lbltab=(cell *)malloc(sc_labnum*sizeof(cell));
    if (lbltab==NULL)
      error(103);               /* insufficient memory */
    memset(lbltab,0,sc_labnum*sizeof(cell));
    pc_resetasm(fin);
    while (pc_readasm(fin,line,sizeof line)!=NULL) {
      stripcomment(line);
      instr=skipwhitespace(line);
      /* ignore empty lines */
      if (*instr=='\0')
        continue;
      if (tolower(*instr)=='l' && *(instr+1)=='.') {
        int lindex=(int)hex2ucell(instr+2,NULL);
        assert(lindex>=0 && lindex<sc_labnum);
        assert(lbltab[lindex]==0);  /* should not already be declared */
        lbltab[lindex]=codeindex;
      } else {
        /* get to the end of the opcode */
        for (params=instr; *params!='\0' && !isspace(*params); params++)
          /* nothing */;
        assert(params>instr);
        unsigned i=findopcode(instr,(int)(params-instr));
        assert(i<countof(opcodelist));
        assert(opcodelist[i].opt_level<=pc_optimize || pc_optimize==0 && opcodelist[i].opt_level<=1);
        if (opcodelist[i].segment==sIN_CSEG)
          codeindex+=opcodelist[i].func(NULL,skipwhitespace(params),opcodelist[i].opcode,codeindex);
      } /* if */
    } /* while */
  } /* if */

  /* Second pass (actually 2 more passes, one for all code and one for all data) */
  for (pass=sIN_CSEG; pass<=sIN_DSEG; pass++) {
    cell codeindex=0; /* address of the current opcode similar to "code_idx" */
    pc_resetasm(fin);
    while (pc_readasm(fin,line,sizeof line)!=NULL) {
      stripcomment(line);
      instr=skipwhitespace(line);
      /* ignore empty lines and labels (labels have a special syntax, so these
       * must be parsed separately) */
      if (*instr=='\0' || tolower(*instr)=='l' && *(instr+1)=='.')
        continue;
      /* get to the end of the opcode */
      for (params=instr; *params!='\0' && !isspace(*params); params++)
        /* nothing */;
      assert(params>instr);
      unsigned i=findopcode(instr,(int)(params-instr));
      assert(i<countof(opcodelist));
      assert(opcodelist[i].name!=NULL);
      assert(opcodelist[i].opt_level<=pc_optimize || pc_optimize==0 && opcodelist[i].opt_level<=1);
      if (opcodelist[i].segment==pass)
        codeindex+=opcodelist[i].func(fout,skipwhitespace(params),opcodelist[i].opcode,codeindex);
    } /* while */
  } /* for */

  if (lbltab!=NULL) {
    free(lbltab);
#   if !defined NDEBUG
      lbltab=NULL;
#   endif
  } /* if */

  if (hdr.size<pc_lengthbin(fout))
    error(114,"file exceeds 2GB limit");  /* capacity exceeded */
  if (!writeerror && (sc_debug & sSYMBOLIC)!=0)
    append_dbginfo(fout);       /* optionally append debug file */

  if (writeerror)
    error(114,"disk full");     /* capacity exceeded */

  /* adjust the header (for Big Endian architectures) */
  size=(int)hdr.cod;    /* save, the value in the header may need to be swapped */
# if BYTE_ORDER==BIG_ENDIAN
    align32(&hdr.size);
    align16(&hdr.magic);
    align16(&hdr.flags);
    align16(&hdr.defsize);
    align32(&hdr.publics);
    align32(&hdr.natives);
    align32(&hdr.libraries);
    align32(&hdr.pubvars);
    align32(&hdr.tags);
    align32(&hdr.nametable);
    align32(&hdr.cod);
    align32(&hdr.dat);
    align32(&hdr.hea);
    align32(&hdr.stp);
    align32(&hdr.cip);
    pc_resetbin(fout,0);
    pc_writebin(fout,&hdr,sizeof hdr);
# endif

  /* return the size of the header (including name tables, but excluding code
   * or data sections)
   */
  return size;
}

static void append_dbginfo(FILE *fout)
{
  AMX_DBG_HDR dbghdr;
  AMX_DBG_LINE dbgline;
  AMX_DBG_SYMBOL dbgsym;
  AMX_DBG_SYMDIM dbgidxtag[sDIMEN_MAX];
  int index,dim,dbgsymdim;
  const char *str,*prevstr,*name,*prevname;
  ucell codeidx,previdx;
  constvalue *constptr;
  char symname[2*sNAMEMAX+16];

  /* header with general information */
  memset(&dbghdr, 0, sizeof dbghdr);
  dbghdr.size=sizeof dbghdr;
  dbghdr.magic=AMX_DBG_MAGIC;
  dbghdr.file_version=CUR_FILE_VERSION;
  dbghdr.amx_version=MIN_AMX_VERSION;

  /* first pass: collect the number of items in various tables */

  /* file table */
  previdx=0;
  prevstr=NULL;
  prevname=NULL;
  for (index=0; (str=get_dbgstring(index))!=NULL; index++) {
    assert(str!=NULL);
    assert(str[0]!='\0' && str[1]==':');
    if (str[0]=='F') {
      codeidx=hex2ucell(str+2,&name);
      if (codeidx!=previdx) {
        if (prevstr!=NULL) {
          assert(prevname!=NULL);
          dbghdr.files++;
          dbghdr.size+=(int32_t)(sizeof(AMX_DBG_FILE)+strlen(prevname));
        } /* if */
        previdx=codeidx;
      } /* if */
      prevstr=str;
      prevname=skipwhitespace(name);
    } /* if */
  } /* for */
  if (prevstr!=NULL) {
    assert(prevname!=NULL);
    dbghdr.files++;
    dbghdr.size+=(int32_t)(sizeof(AMX_DBG_FILE)+strlen(prevname));
  } /* if */

  /* line number table */
  for (index=0; (str=get_dbgstring(index))!=NULL; index++) {
    assert(str!=NULL);
    assert(str[0]!='\0' && str[1]==':');
    if (str[0]=='L') {
      dbghdr.lines++;
      dbghdr.size+=sizeof(AMX_DBG_LINE);
    } /* if */
  } /* for */

  /* symbol table */
  for (index=0; (str=get_dbgstring(index))!=NULL; index++) {
    assert(str!=NULL);
    assert(str[0]!='\0' && str[1]==':');
    if (str[0]=='S') {
      dbghdr.symbols++;
      str=strchr(str+2,':');
      assert(str!=NULL);
      name=skipwhitespace(str+1);
      str=strchr(name,' ');
      assert(str!=NULL);
      assert((unsigned)(str-name)<sizeof symname);
      strlcpy(symname,name,(int)(str-name)+1);
      dbghdr.size+=(int32_t)(sizeof(AMX_DBG_SYMBOL)+strlen(symname));
      if ((prevstr=strchr(name,'['))!=NULL) {
        while (*(prevstr=skipwhitespace(prevstr+1))!=']') {
          dbghdr.size+=sizeof(AMX_DBG_SYMDIM);
          hex2ucell(prevstr,&prevstr);
        } /* while */
      } /* if */
    } /* if */
  } /* for */

  /* tag table */
  for (constptr=tagname_tab.next; constptr!=NULL; constptr=constptr->next) {
    assert(strlen(constptr->name)>0);
    dbghdr.tags++;
    dbghdr.size+=(int32_t)(sizeof(AMX_DBG_TAG)+strlen(constptr->name));
  } /* for */

  /* automaton table */
  for (constptr=sc_automaton_tab.next; constptr!=NULL; constptr=constptr->next) {
    assert(constptr->index==0 && strlen(constptr->name)==0 || strlen(constptr->name)>0);
    dbghdr.automatons++;
    dbghdr.size+=(int32_t)(sizeof(AMX_DBG_AUTOMATON)+strlen(constptr->name));
  } /* for */

  /* state table */
  for (constptr=sc_state_tab.next; constptr!=NULL; constptr=constptr->next) {
    assert(strlen(constptr->name)>0);
    dbghdr.states++;
    dbghdr.size+=(int32_t)(sizeof(AMX_DBG_STATE)+strlen(constptr->name));
  } /* for */


  /* pass 2: generate the tables */
# if BYTE_ORDER==BIG_ENDIAN
    align32((uint32_t*)&dbghdr.size);
    align16(&dbghdr.magic);
    align16(&dbghdr.flags);
    align16(&dbghdr.files);
    align16(&dbghdr.lines);
    align16(&dbghdr.symbols);
    align16(&dbghdr.tags);
    align16(&dbghdr.automatons);
    align16(&dbghdr.states);
# endif
  writeerror |= !pc_writebin(fout,&dbghdr,sizeof dbghdr);

  /* file table */
  previdx=0;
  prevstr=NULL;
  prevname=NULL;
  for (index=0; (str=get_dbgstring(index))!=NULL; index++) {
    assert(str!=NULL);
    assert(str[0]!='\0' && str[1]==':');
    if (str[0]=='F') {
      codeidx=hex2ucell(str+2,&name);
      if (codeidx!=previdx) {
        if (prevstr!=NULL) {
          assert(prevname!=NULL);
#         if BYTE_ORDER==BIG_ENDIAN
            align32(&previdx);
#         endif
          writeerror |= !pc_writebin(fout,&previdx,sizeof(uint32_t));
          writeerror |= !pc_writebin(fout,prevname,(int)strlen(prevname)+1);
        } /* if */
        previdx=codeidx;
      } /* if */
      prevstr=str;
      prevname=skipwhitespace(name);
    } /* if */
  } /* for */
  if (prevstr!=NULL) {
    assert(prevname!=NULL);
#   if BYTE_ORDER==BIG_ENDIAN
      align32(&previdx);
#   endif
    writeerror |= !pc_writebin(fout,&previdx,sizeof(uint32_t));
    writeerror |= !pc_writebin(fout,prevname,(int)strlen(prevname)+1);
  } /* if */

  /* line number table */
  for (index=0; (str=get_dbgstring(index))!=NULL; index++) {
    assert(str!=NULL);
    assert(str[0]!='\0' && str[1]==':');
    if (str[0]=='L') {
      dbgline.address=(uint32_t)hex2ucell(str+2,&str);
      dbgline.line=(int32_t)hex2ucell(str,NULL);
#     if BYTE_ORDER==BIG_ENDIAN
        align32(&dbgline.address);
        align32(&dbgline.line);
#     endif
      writeerror |= !pc_writebin(fout,&dbgline,sizeof dbgline);
    } /* if */
  } /* for */

  /* symbol table */
  for (index=0; (str=get_dbgstring(index))!=NULL; index++) {
    assert(str!=NULL);
    assert(str[0]!='\0' && str[1]==':');
    if (str[0]=='S') {
      dbgsym.address=(uint32_t)hex2ucell(str+2,&str);
      dbgsym.tag=(int16_t)hex2ucell(str,&str);
      str=skipwhitespace(str);
      assert(*str==':');
      name=skipwhitespace(str+1);
      str=strchr(name,' ');
      assert(str!=NULL);
      assert((unsigned)(str-name)<sizeof symname);
      strlcpy(symname,name,(int)(str-name)+1);
      dbgsym.codestart=(uint32_t)hex2ucell(str,&str);
      dbgsym.codeend=(uint32_t)hex2ucell(str,&str);
      dbgsym.ident=(char)hex2ucell(str,&str);
      dbgsym.scope=(char)hex2ucell(str,&str);
      dbgsym.dim=0;
      str=skipwhitespace(str);
      if (*str=='[') {
        while (*(str=skipwhitespace(str+1))!=']') {
          dbgidxtag[dbgsym.dim].size=(uint32_t)hex2ucell(str,&str);
          dbgidxtag[dbgsym.dim].tag=0;  /* index tag currently unused in debug info */
          dbgsym.dim++;
        } /* while */
      } /* if */
      dbgsymdim = dbgsym.dim;
#     if BYTE_ORDER==BIG_ENDIAN
        align32(&dbgsym.address);
        align16(&dbgsym.tag);
        align32(&dbgsym.codestart);
        align32(&dbgsym.codeend);
        align16(&dbgsym.dim);
#     endif
      writeerror |= !pc_writebin(fout,&dbgsym.address,sizeof dbgsym.codeend);
      writeerror |= !pc_writebin(fout,&dbgsym.tag,sizeof dbgsym.tag);
      writeerror |= !pc_writebin(fout,&dbgsym.codestart,sizeof dbgsym.codeend);
      writeerror |= !pc_writebin(fout,&dbgsym.codeend,sizeof dbgsym.codeend);
      writeerror |= !pc_writebin(fout,&dbgsym.ident,sizeof dbgsym.ident);
      writeerror |= !pc_writebin(fout,&dbgsym.scope,sizeof dbgsym.scope);
      writeerror |= !pc_writebin(fout,&dbgsym.dim,sizeof dbgsym.dim);
      writeerror |= !pc_writebin(fout,symname,(int)strlen(symname)+1);
      for (dim=0; dim<dbgsymdim; dim++) {
#       if BYTE_ORDER==BIG_ENDIAN
          align16(&dbgidxtag[dim].tag);
          align32(&dbgidxtag[dim].size);
#       endif
        writeerror |= !pc_writebin(fout,&dbgidxtag[dim].tag,sizeof dbgidxtag[dim].tag);
        writeerror |= !pc_writebin(fout,&dbgidxtag[dim].size,sizeof dbgidxtag[dim].size);
      } /* for */
    } /* if */
  } /* for */

  /* tag table */
  for (constptr=tagname_tab.next; constptr!=NULL; constptr=constptr->next) {
    assert(strlen(constptr->name)>0);
    int16_t id1=(int16_t)(constptr->value & TAGMASK);
#   if BYTE_ORDER==BIG_ENDIAN
      align16(&id1);
#   endif
    writeerror |= !pc_writebin(fout,&id1,sizeof id1);
    writeerror |= !pc_writebin(fout,constptr->name,(int)strlen(constptr->name)+1);
  } /* for */

  /* automaton table */
  for (constptr=sc_automaton_tab.next; constptr!=NULL; constptr=constptr->next) {
    assert(constptr->index==0 && strlen(constptr->name)==0 || strlen(constptr->name)>0);
    int16_t id1=(int16_t)constptr->index;
    ucell addr=(ucell)constptr->value;
#   if BYTE_ORDER==BIG_ENDIAN
      align16(&id1);
      align32(&addr);
#   endif
    writeerror |= !pc_writebin(fout,&id1,sizeof id1);
    writeerror |= !pc_writebin(fout,&addr,sizeof(uint32_t));
    writeerror |= !pc_writebin(fout,constptr->name,(int)strlen(constptr->name)+1);
  } /* for */

  /* state table */
  for (constptr=sc_state_tab.next; constptr!=NULL; constptr=constptr->next) {
    assert(strlen(constptr->name)>0);
    int16_t id1=(int16_t)constptr->value;
    int16_t id2=(int16_t)constptr->index;
#   if BYTE_ORDER==BIG_ENDIAN
      align16(&id1);
      align16(&id2);
#   endif
    writeerror |= !pc_writebin(fout,&id1,sizeof id1);
    writeerror |= !pc_writebin(fout,&id2,sizeof id2);
    writeerror |= !pc_writebin(fout,constptr->name,(int)strlen(constptr->name)+1);
  } /* for */

  delete_dbgstringtable();
}

/** is_sequence_point() checks whether a pseudo-assembly instruction is a
 *  sequence point for a register.
 *
 *  \param instr        [in] A pointer to the instruction (or special comment).
 *  \param reg          A bit field for the register(s) to test for the sequence
 *                      point: `1<<sPRI` or `1<<sALT`, or both.
 *
 *  \return `true` for a seuqnce point, `false` otherwise.
 *
 *  \note A sequence point is a point in the code where the value of a register
 *        will not be used. An instruction changing the register just before a
 *        sequence point is therefore redundant.
 *
 *  \note If an instruction is a sequence point for *both* `sPRI` and `sALT`, it
 *        is also a sequence point for either `sPRI` or `sALT`.
 */
SC_FUNC bool is_sequence_point(const char *instr,unsigned reg)
{
  assert(instr!=NULL);
  assert(reg!=0);
  const char *params;
  for (params=instr; *params!='\0' && !isspace(*params); params++)
    /* nothing */;
  assert(params>instr);
  int len=(params-instr);
  /* The Pawn compiler does not carry over code or information on registers from
   * one expression to the next, so the comments that mark the end of an
   * expression are always sequence points for the optimizer
   */
  if (len==6 && strncmp(instr,";;expr",len)==0 || len==5 && strncmp(instr,";;arg",len)==0)
    return true;
  unsigned idx=findopcode(instr,len);
  assert(idx<countof(opcodelist));
  return ((opcodelist[idx].seqpoint & reg)==reg);
}

