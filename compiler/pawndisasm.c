/*  Pawn disassembler  - crude, but hopefully useful
 *
 *  Copyright (c) 2007-2026, CompuPhase and contributors
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
 *  Version: $Id: pawndisasm.c 7701 2026-09-29 11:09:02Z thiadmer $
 */
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined FORTIFY
# include <alloc/Fortify.h>
#endif
#if !defined PAWN_CELL_SIZE
# define PAWN_CELL_SIZE 64 /* by default, maximum cell size = 64-bit */
#endif
#include "../amx/osdefs.h"
#include "../amx/amx.h"
#include "../amx/amxdbg.h"
#if (defined _MSC_VER || defined __GNUC__ || defined __clang__) && !defined __APPLE__
# include "lstring.h"
#endif

#include "svnrev.h"
#define VERSION_STR "4.1." SVNREV_STR
#define VERSION_INT 0x0401

#if !defined countof
# define countof(a)     (sizeof(a) / sizeof((a)[0]))
#endif

static FILE *fpamx;
static AMX_HEADER amxhdr;
static AMX_DBG amxdbg;
static int pc_cellsize;
static ucell pc_cellmask;
static ucell pc_cellmask_p;

typedef cell (*OPCODE_PROC)(FILE *ftxt,const cell *params,cell opcode,ucell cip);

static cell parm0(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell parm1(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell parm2(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell parmx(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell parm1_p(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell parmx_p(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell do_proc(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell do_call(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell do_sysreq(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell do_sysreq_n(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell do_jump(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell do_switch(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell casetbl(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell casetbl_ovl(FILE *ftxt,const cell *params,cell opcode,ucell cip);
static cell do_break(FILE *ftxt,const cell *params,cell opcode,ucell cip);


typedef struct tagLABEL {
  struct tagLABEL *next;
  int seq;
  ucell address;
} LABEL;

static LABEL labelroot = { 0 };


typedef struct tagOPCODE {
  cell opcode;
  char *name;
  OPCODE_PROC func;
} OPCODE;

static OPCODE opcodelist[] = {
  {  0, "nop",         parm0 },
  {  1, "load.pri",    parm1 },
  {  2, "load.alt",    parm1 },
  {  3, "load.s.pri",  parm1 },
  {  4, "load.s.alt",  parm1 },
  {  5, "lref.s.pri",  parm1 },
  {  6, "lref.s.alt",  parm1 },
  {  7, "load.i",      parm0 },
  {  8, "lodb.i",      parm1 },
  {  9, "const.pri",   parm1 },
  { 10, "const.alt",   parm1 },
  { 11, "addr.pri",    parm1 },
  { 12, "addr.alt",    parm1 },
  { 13, "stor",        parm1 },
  { 14, "stor.s",      parm1 },
  { 15, "sref.s",      parm1 },
  { 16, "stor.i",      parm0 },
  { 17, "strb.i",      parm1 },
  { 18, "align.pri",   parm1 },
  { 19, "lctrl",       parm1 },
  { 20, "sctrl",       parm1 },
  { 21, "xchg",        parm0 },
  { 22, "push.pri",    parm0 },
  { 23, "push.alt",    parm0 },
  { 24, "pushr.pri",   parm0 },
  { 25, "pop.pri",     parm0 },
  { 26, "pop.alt",     parm0 },
  { 27, "pick",        parm1 },
  { 28, "stack",       parm1 },
  { 29, "heap",        parm1 },
  { 30, "proc",        do_proc },
  { 31, "ret",         parm0 },
  { 32, "retn",        parm0 },
  { 33, "call",        do_call },
  { 34, "jump",        do_jump },
  { 35, "jzer",        do_jump },
  { 36, "jnz",         do_jump },
  { 37, "shl",         parm0 },
  { 38, "shr",         parm0 },
  { 39, "sshr",        parm0 },
  { 40, "shl.c.pri",   parm1 },
  { 41, "shl.c.alt",   parm1 },
  { 42, "smul",        parm0 },
  { 43, "sdiv",        parm0 },
  { 44, "add",         parm0 },
  { 45, "sub",         parm0 },
  { 46, "and",         parm0 },
  { 47, "or",          parm0 },
  { 48, "xor",         parm0 },
  { 49, "not",         parm0 },
  { 50, "neg",         parm0 },
  { 51, "invert",      parm0 },
  { 52, "eq",          parm0 },
  { 53, "neq",         parm0 },
  { 54, "sless",       parm0 },
  { 55, "sleq",        parm0 },
  { 56, "sgrtr",       parm0 },
  { 57, "sgeq",        parm0 },
  { 58, "inc.pri",     parm0 },
  { 59, "inc.alt",     parm0 },
  { 60, "inc.i",       parm0 },
  { 61, "dec.pri",     parm0 },
  { 62, "dec.alt",     parm0 },
  { 63, "dec.i",       parm0 },
  { 64, "movs",        parm1 },
  { 65, "cmps",        parm1 },
  { 66, "fill",        parm1 },
  { 67, "halt",        parm1 },
  { 68, "bounds",      parm1 },
  { 69, "sysreq",      do_sysreq },
  { 70, "switch",      do_switch },
  { 71, "swap.pri",    parm0 },
  { 72, "swap.alt",    parm0 },
  { 73, "break",       do_break },
  { 74, "casetbl",     casetbl },
  { 75, "sysreq.d",    parm1 }, /* not generated by the compiler */
  { 76, "sysreq.nd",   parm2 }, /* not generated by the compiler */
  { 77, "call.ovl",    parm1 },
  { 78, "retn.ovl",    parm0 },
  { 79, "switch.ovl",  do_switch },
  { 80, "casetbl.ovl", casetbl_ovl },
  { 81, "lidx",        parm0 },
  { 82, "lidx.b",      parm1 },
  { 83, "idxaddr",     parm0 },
  { 84, "idxaddr.b",   parm1 },
  { 85, "push.c",      parm1 },
  { 86, "push",        parm1 },
  { 87, "push.s",      parm1 },
  { 88, "push.adr",    parm1 },
  { 89, "pushr.c",     parm1 },
  { 90, "pushr.s",     parm1 },
  { 91, "pushr.adr",   parm1 },
  { 92, "jeq",         do_jump },
  { 93, "jneq",        do_jump },
  { 94, "jsless",      do_jump },
  { 95, "jsleq",       do_jump },
  { 96, "jsgrtr",      do_jump },
  { 97, "jsgeq",       do_jump },
  { 98, "sdiv.inv",    parm0 },
  { 99, "sub.inv",     parm0 },
  {100, "add.c",       parm1 },
  {101, "smul.c",      parm1 },
  {102, "zero.pri",    parm0 },
  {103, "zero.alt",    parm0 },
  {104, "zero",        parm1 },
  {105, "zero.s",      parm1 },
  {106, "eq.c.pri",    parm1 },
  {107, "eq.c.alt",    parm1 },
  {108, "inc",         parm1 },
  {109, "inc.s",       parm1 },
  {110, "dec",         parm1 },
  {111, "dec.s",       parm1 },
  {112, "sysreq.n",    do_sysreq_n },
  {113, "pushm.c",     parmx },
  {114, "pushm",       parmx },
  {115, "pushm.s",     parmx },
  {116, "pushm.adr",   parmx },
  {117, "pushrm.c",    parmx },
  {118, "pushrm.s",    parmx },
  {119, "pushrm.adr",  parmx },
  {120, "load2",       parm2 },
  {121, "load2.s",     parm2 },
  {122, "const",       parm2 },
  {123, "const.s",     parm2 },
  {124, "load.p.pri",  parm1_p },
  {125, "load.p.alt",  parm1_p },
  {126, "load.p.s.pri",parm1_p },
  {127, "load.p.s.alt",parm1_p },
  {128, "lref.p.s.pri",parm1_p },
  {129, "lref.p.s.alt",parm1_p },
  {130, "lodb.p.i",    parm1_p },
  {131, "const.p.pri", parm1_p },
  {132, "const.p.alt", parm1_p },
  {133, "addr.p.pri",  parm1_p },
  {134, "addr.p.alt",  parm1_p },
  {135, "stor.p",      parm1_p },
  {136, "stor.p.s",    parm1_p },
  {137, "sref.p.s",    parm1_p },
  {138, "strb.p.i",    parm1_p },
  {139, "lidx.p.b",    parm1_p },
  {140, "idxaddr.p.b", parm1_p },
  {141, "align.p.pri", parm1_p },
  {142, "push.p.c",    parm1_p },
  {143, "push.p",      parm1_p },
  {144, "push.p.s",    parm1_p },
  {145, "push.p.adr",  parm1_p },
  {146, "pushr.p.c",   parm1_p },
  {147, "pushr.p.s",   parm1_p },
  {148, "pushr.p.adr", parm1_p },
  {149, "pushm.p.c",   parmx_p },
  {150, "pushm.p",     parmx_p },
  {151, "pushm.p.s",   parmx_p },
  {152, "pushm.p.adr", parmx_p },
  {153, "pushrm.p.c",  parmx_p },
  {154, "pushrm.p.s",  parmx_p },
  {155, "pushrm.p.adr",parmx_p },
  {156, "stack.p",     parm1_p },
  {157, "heap.p",      parm1_p },
  {158, "shl.p.c.pri", parm1_p },
  {159, "shl.p.c.alt", parm1_p },
  {160, "add.p.c",     parm1_p },
  {161, "smul.p.c",    parm1_p },
  {162, "zero.p",      parm1_p },
  {163, "zero.p.s",    parm1_p },
  {164, "eq.p.c.pri",  parm1_p },
  {165, "eq.p.c.alt",  parm1_p },
  {166, "inc.p",       parm1_p },
  {167, "inc.p.s",     parm1_p },
  {168, "dec.p",       parm1_p },
  {169, "dec.p.s",     parm1_p },
  {170, "movs.p",      parm1_p },
  {171, "cmps.p",      parm1_p },
  {172, "fill.p",      parm1_p },
  {173, "halt.p",      parm1_p },
  {174, "bounds.p",    parm1_p },
};


/* reads one cell at "cip", with the width the file declares */
static cell fetch_opcode(const unsigned char *cip)
{
  switch (pc_cellsize) {
  case 2:
    return (cell)*(uint16_t*)cip;
  case 4:
    return (cell)*(uint32_t*)cip;
  case 8:
    return (cell)*(uint64_t*)cip;
  } /* switch */
  assert(0);
  return 0;
}

/* The opcode is an index into opcodelist[]; low bits are masked to handle
 * packed instructions. Returns NULL for an opcode that is not in the table.
 */
static OPCODE_PROC find_opcode(cell opc)
{
  ucell idx;

  switch (pc_cellsize) {
  case 2:
    idx=(ucell)(opc & 0xff);
    break;
  case 4:
    idx=(ucell)(opc & 0xffff);
    break;
  case 8:
    idx=(ucell)(opc & 0xffffffffLU);
    break;
  default:
    assert(0);
    return NULL;
  } /* switch */
  return (idx<countof(opcodelist)) ? opcodelist[(int)idx].func : NULL;
}

static void label_add(ucell address)
{
  LABEL *item, *last;
  int seq=0;

  /* find the last existing label entry */
  for (last=&labelroot; last->next!=NULL; last=last->next)
    seq++;
  if ((item=malloc(sizeof(LABEL)))!=NULL) {
    item->seq=seq;
    item->address=address;
    item->next=NULL;
    last->next=item;
  } /* if */
}

static const char *label_get(ucell address)
{
  static char str[10];
  LABEL *item;
  for (item=labelroot.next; item!=NULL && item->address!=address; item=item->next)
    /* nothing */;
  if (item!=NULL)
    sprintf(str,"L%d:",item->seq);
  else
    str[0]='\0';
  return str;
}

static void label_deletall(void)
{
  LABEL *item;
  while (labelroot.next!=NULL) {
    item=labelroot.next;
    labelroot.next=item->next;
    free(item);
  }  /* while */
}


#define NUMENTRIES(hdr,field,nextfield) \
                        (unsigned)(((hdr).nextfield - (hdr).field) / (hdr).defsize)

static int read_hdr_name(FILE *fpamx,char *name,size_t namesz,unsigned offset)
{
  assert(fpamx!=NULL);
  assert(name!=NULL && namesz>0);
  assert(offset>0);

  fseek(fpamx,offset,SEEK_SET);
  int c;
  size_t idx=0;
  while (idx<namesz-1 && (c=fgetc(fpamx))!=EOF && c!=0)
    name[idx++]=(char)c;
  name[idx]='\0'; /* force zero-terminate, the file need not hold a terminator */
  return idx;
}

static int lookup_native(FILE *fpamx,char *name,size_t namesz,int id)
{
  assert(fpamx!=NULL);
  assert(name!=NULL && namesz>0);
  assert(id>=0);
  *name='\0';

  /* jump to the record in the native function table */
  int numnatives=NUMENTRIES(amxhdr,natives,libraries);
  if (id<0 || id>=numnatives)
    return 0;
  fseek(fpamx,amxhdr.natives+id*amxhdr.defsize,SEEK_SET);
  AMX_FUNCSTUB func;
  if (fread(&func,sizeof func,1,fpamx)!=1)
    return 0;           /* table runs past the end of the file */
  return read_hdr_name(fpamx,name,namesz,func.nameofs);
}


static void print_opcode(FILE *ftxt,cell opcode,ucell cip)
{
  assert(ftxt!=NULL);
  assert(opcodelist[(int)(opcode & 0x0000ffff)].opcode==(opcode & 0x0000ffff));
  fprintf(ftxt,"%08lx  %-6s%s ",(unsigned long)cip,label_get(cip),opcodelist[(int)(opcode & 0x0000ffff)].name);
}

static void print_param(FILE *ftxt,cell param,bool ispacked,bool eol)
{
  assert(ftxt!=NULL);
  int psize=pc_cellsize;
  param&=pc_cellmask;
  if (ispacked) {
    param&=pc_cellmask_p;
    psize/=2;
  } /* if */
  switch (psize) {
  case 1:
    fprintf(ftxt,"%02" PRIx8,(uint8_t)param);
    break;
  case 2:
    fprintf(ftxt,"%04" PRIx16,(uint16_t)param);
    break;
  case 4:
    fprintf(ftxt,"%08" PRIx32,(uint32_t)param);
    break;
  case 8:
    fprintf(ftxt,"%016" PRIx64,(uint64_t)param);
    break;
  default:
    assert(0);
  } /* switch */
  if (eol)
    fprintf(ftxt,"\n");
  else
    fprintf(ftxt," ");
}

/* Bounds of the block loaded from the file. Every parameter the opcode handlers
 * read comes out of it, at an offset the file itself supplies, so the read has
 * to be checked against these.
 */
static const unsigned char *code_start=NULL;
static const unsigned char *code_end=NULL;

/* how many cells are still readable from "params" on */
static cell params_available(const cell *params)
{
  const unsigned char *p=(const unsigned char*)params;
  if (code_start==NULL || p<code_start || p>=code_end)
    return 0;
  return (cell)((code_end-p)/pc_cellsize);
}

/* The number of cases in a case table comes from the file, and the table that
 * follows is two cells per case. Cap it at what the block can still hold, so a
 * corrupted count cannot walk the loop past the end.
 */
static cell clamp_casecount(const cell *params);

static cell get_param(const cell *params, int index)
{
  if (index>=params_available(params))
    return 0;                   /* past the end of the block */
  ucell value;
  switch (pc_cellsize) {
  case 2:
    value=*((uint16_t*)params+index);
    break;
  case 4:
    value=*((uint32_t*)params+index);
    break;
  case 8:
    value=*((uint64_t*)params+index);
    break;
  default:
    assert(0);
  } /* switch */
  if (pc_cellsize<(int)sizeof(ucell)) {
    /* sign-extend */
    assert(pc_cellsize>0);
    int shift=pc_cellsize*8;
    ucell mask=(ucell)1<<(shift-1);
    if ((value & mask)!=0)
      value|=(((ucell)~0U)<<pc_cellsize*8);
  }
  return (cell)value;
}

static cell parm0(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  (void)params;
  if (ftxt!=NULL) {
    print_opcode(ftxt,opcode,cip);
    fprintf(ftxt,"\n");
  } /* if */
  return 1;
}

static cell parm1(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  if (ftxt!=NULL) {
    print_opcode(ftxt,opcode,cip);
    print_param(ftxt,get_param(params,0),false,true);
  } /* if */
  return 2;
}

static cell parm2(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  if (ftxt!=NULL) {
    print_opcode(ftxt,opcode,cip);
    print_param(ftxt,get_param(params,0),false,false);
    print_param(ftxt,get_param(params,1),false,true);
  } /* if */
  return 3;
}

static cell parmx(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  int count=(int)get_param(params,0);
  if (ftxt!=NULL) {
    int idx;
    print_opcode(ftxt,opcode,cip);
    print_param(ftxt,count,false,false);
    for (idx=0; idx<count-1; idx++)
      print_param(ftxt,get_param(params,idx+1),false,false);
    print_param(ftxt,get_param(params,idx+1),false,true);
  } /* if */
  return count+2;
}

static cell parm1_p(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  (void)params;
  if (ftxt!=NULL) {
    print_opcode(ftxt,opcode,cip);
    print_param(ftxt,(cell)((ucell)opcode>>(pc_cellsize*4)),true,true);
  } /* if */
  return 1;
}

static cell parmx_p(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  int count=(int)((ucell)opcode>>(pc_cellsize*4));
  if (ftxt!=NULL) {
    int idx;
    print_opcode(ftxt,opcode,cip);
    print_param(ftxt,count,true,false);
    for (idx=0; idx<count-1; idx++)
      print_param(ftxt,get_param(params,idx),false,false);
    print_param(ftxt,get_param(params,idx),false,true);
  } /* if */
  return count+1;
}

static cell do_proc(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  (void)params;
  if (ftxt!=NULL) {

    fprintf(ftxt,"\n");

    char name[sNAMEMAX+1]="";

    /* if debug information is available, use it to look up the function */
    const char *ptr;
    if (amxdbg.hdr!=NULL && dbg_LookupFunction(&amxdbg,cip,&ptr)==AMX_ERR_NONE)
      strlcpy(name,ptr,countof(name));

    if (strlen(name)==0) {
      /* no debug info; try to find the procedure in the table (only works for
       * a public function)
       */
      int nameoffset=-1;
      if ((amxhdr.flags & AMX_FLAG_OVERLAY)!=0) {
        //??? first find the address in the overlay table
        //??? find the overlay index in the public function table
      } else {
        /* find the address in the public function table */
        int numpublics=NUMENTRIES(amxhdr,publics,natives);
        fseek(fpamx,amxhdr.publics,SEEK_SET);
        for (int idx=0; idx<numpublics && nameoffset<0; idx++) {
          AMX_FUNCSTUB func;
          if (fread(&func,sizeof func,1,fpamx)!=1)
            break;                      /* table runs past the end of the file */
          if (func.address==cip)
            nameoffset=func.nameofs;
        } /* for */
      } /* if */
      if (nameoffset>=0)
        read_hdr_name(fpamx,name,countof(name),nameoffset);
    } /* if */

    print_opcode(ftxt,opcode,cip);
    if (strlen(name)>0)
      fprintf(ftxt,"\t\t; %s",name);
    fprintf(ftxt,"\n");
  } /* if */
  return 1;
}

static cell do_call(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  if (ftxt!=NULL) {
    print_opcode(ftxt,opcode,cip);
    ucell addr=get_param(params,0)+cip;
    print_param(ftxt,addr,false,false);

    const char *func;
    if (amxdbg.hdr!=NULL && dbg_LookupFunction(&amxdbg,addr,&func)==AMX_ERR_NONE)
      fprintf(ftxt,"\t; %s",func);

    fprintf(ftxt,"\n");
  } /* if */
  return 2;
}

static cell do_jump(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  if (ftxt!=NULL) {
    ucell address=get_param(params,0)+cip;
    print_opcode(ftxt,opcode,cip);
    print_param(ftxt,address,false,false);
    fprintf(ftxt,"\t; %s\n",label_get(address));
  } /* if */
  return 2;
}

static cell do_sysreq(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  if (ftxt!=NULL) {
    print_opcode(ftxt,opcode,cip);
    cell ntv_id=get_param(params,0);
    print_param(ftxt,ntv_id,false,false);

    char name[sNAMEMAX+1];
    if (lookup_native(fpamx,name,countof(name),ntv_id))
      fprintf(ftxt,"\t; %s",name);

    fprintf(ftxt,"\n");
  } /* if */
  return 2;
}

static cell do_sysreq_n(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  if (ftxt!=NULL) {
    print_opcode(ftxt,opcode,cip);
    cell ntv_id=get_param(params,0);
    print_param(ftxt,ntv_id,false,false);
    print_param(ftxt,get_param(params,1),false,false);

    char name[sNAMEMAX+1];
    if (lookup_native(fpamx,name,countof(name),ntv_id))
      fprintf(ftxt,"\t; %s",name);

    fprintf(ftxt,"\n");
  } /* if */
  return 3;
}

static cell do_switch(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  if (ftxt!=NULL) {
    print_opcode(ftxt,opcode,cip);
    print_param(ftxt,get_param(params,0)+cip,false,true);
  } /* if */
  return 2;
}

static cell clamp_casecount(const cell *params)
{
  cell num=get_param(params,0)+1;
  cell room=(params_available(params)-1)/2;

  if (num<1)
    num=1;
  if (num>room+1)
    num=room+1;
  return num;
}

static cell casetbl(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  cell num=clamp_casecount(params);
  if (ftxt!=NULL) {
    int idx;
    print_opcode(ftxt,opcode,cip);
    print_param(ftxt,get_param(params,0),false,false);
    print_param(ftxt,get_param(params,1)+cip+pc_cellsize,false,true);
    for (idx=1; idx<num; idx++) {
      fprintf(ftxt,"                  ");
      print_param(ftxt,get_param(params,2*idx),false,false);
      print_param(ftxt,get_param(params,2*idx+1)+cip+(2*idx+1)*pc_cellsize,false,true);
    } /* for */
  } /* if */
  return 2*num+1;
}

static cell casetbl_ovl(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  cell num=clamp_casecount(params);
  if (ftxt!=NULL) {
    int idx;
    print_opcode(ftxt,opcode,cip);
    print_param(ftxt,get_param(params,0),false,false);
    print_param(ftxt,get_param(params,1),false,true);
    for (idx=1; idx<num; idx++) {
      fprintf(ftxt,"                  ");
      print_param(ftxt,get_param(params,2*idx),false,false);
      print_param(ftxt,get_param(params,2*idx+1),false,true);
    } /* for */
  } /* if */
  return 2*num+1;
}

static cell do_break(FILE *ftxt,const cell *params,cell opcode,ucell cip)
{
  (void)params;
  if (ftxt!=NULL) {
    print_opcode(ftxt,opcode,cip);

    long line;
    if (amxdbg.hdr!=NULL && dbg_LookupLineNumber(&amxdbg,cip,&line)==AMX_ERR_NONE)
      fprintf(ftxt,"\t\t; line %ld",line);

    fprintf(ftxt,"\n");
  } /* if */
  return 1;
}

static void addchars(char *str,uint64_t value,int pos)
{
  int i;
  str+=pos*pc_cellsize;
  for (i=0; i<pc_cellsize; i++) {
    int v=(value >> 8*(pc_cellsize-1)) & 0xff;
    value <<= 8;
    *str++= (v>=32) ? (char)v : ' ';
  } /* for */
  *str='\0';
}

static int fclose_not_stdout(FILE *file)
{
  /* close the file, unless it is stdout */
  if (file!=stdout)
    return fclose(file);
  return 0;
}

static bool disassemble(FILE *fpamx,FILE *fplist,uint32_t addr_from,uint32_t addr_to)
{
  uint32_t codesize=amxhdr.hea-amxhdr.cod;   /* size for both code and data */

  if (addr_from==0) {
    fprintf(fplist,";File version: %d\n",amxhdr.file_version);
    fprintf(fplist,";Cell size: %d bits\n",8*pc_cellsize);
    fprintf(fplist,";Flags:        ");
    if ((amxhdr.flags & AMX_FLAG_SLEEP)!=0)
      fprintf(fplist,"sleep(re-entrant) ");
    if ((amxhdr.flags & AMX_FLAG_OVERLAY)!=0)
      fprintf(fplist,"overlays ");
    if ((amxhdr.flags & AMX_FLAG_DEBUG)!=0)
      fprintf(fplist,"debug-info ");
    fprintf(fplist,"\n\n");
  } /* if */

  /* load the code block */
  unsigned char *code=(unsigned char*)malloc(codesize);
  if (code==NULL) {
    fprintf(stderr,"Error: insufficient memory: need %d bytes\n",codesize);
    fclose(fpamx);
    fclose_not_stdout(fplist);
    return false;
  } /* if */

  /* read the file */
  code_start=code;
  code_end=code+codesize;
  fseek(fpamx,amxhdr.cod,SEEK_SET);
  if (fread(code,1,codesize,fpamx)!=(size_t)codesize) {
    fprintf(stderr,"Error: not a valid AMX file: the code block runs past the end of the file\n");
    free(code);
    fclose(fpamx);
    fclose_not_stdout(fplist);
    return false;
  } /* if */

  /* do a first run through the code to get jump targets (for labels) */
  unsigned char *cip=code;
  codesize=amxhdr.dat-amxhdr.cod;
  while ((uint32_t)(cip-code)<codesize) {
    cell opc=fetch_opcode(cip);
    OPCODE_PROC func=find_opcode(opc);
    if (func==NULL)
      break;                    /* not a valid opcode: stop scanning for labels */
    ucell addr_cip=(ucell)(cip-code);
    if (func==do_jump)
      label_add(get_param((cell*)(cip+pc_cellsize),0)+addr_cip);
    opc=func(NULL,(cell*)(cip+pc_cellsize),opc,addr_cip);
    if (opc<1 || (uint32_t)opc>(codesize-addr_cip)/pc_cellsize)
      break;                    /* the instruction does not fit in the block */
    cip+=pc_cellsize*opc;
  } /* while */

  /* browse through the code */
  cip=code;
  codesize=amxhdr.dat-amxhdr.cod;
  while ((uint32_t)(cip-code)<codesize) {
    cell opc=fetch_opcode(cip);
    OPCODE_PROC func=find_opcode(opc);
    if (func==NULL) {
      fprintf(fplist,"\n; invalid opcode %08lx at %08lx\n",
              (long)opc,(long)(cip-code));
      break;
    } /* if */
    ucell addr_cip=(ucell)(cip-code);
    bool inrange= (addr_from<=addr_cip && addr_cip<=addr_to);
    opc=func(inrange ? fplist : NULL,(cell*)(cip+pc_cellsize),opc,addr_cip);
    if (opc<1 || (uint32_t)opc>(codesize-addr_cip)/pc_cellsize) {
      fprintf(fplist,"\n; instruction at %08lx runs past the end of the code\n",
              (long)(cip-code));
      break;
    } /* if */
    cip+=pc_cellsize*opc;
  } /* while */

  /* dump the data section by default, but not if a code range was requested */
  if (addr_to==ULONG_MAX) {
    fprintf(fplist,"\n\nDATA");
    cip=code+(amxhdr.dat-amxhdr.cod);
    codesize=amxhdr.hea-amxhdr.cod;
    int count=0;
    int cells_per_line= (pc_cellsize==8) ? 2 : 4;
    char line[128]="";
    while ((uint32_t)(cip-code)<codesize) {
      assert(strlen(line)+16<countof(line));
      if (count==0) {
        if (strlen(line)>0) {
          fprintf(fplist,"\t; %s",line);
          line[0]='\0';
        } /* if */
        fprintf(fplist,"\n%08lx  ",(long)((cip-code)-(amxhdr.dat-amxhdr.cod)));
      } /* if */
      switch (pc_cellsize) {
      case 2:
        fprintf(fplist,"%04" PRIx16 " ",*(uint16_t*)cip);
        addchars(line,*(uint16_t*)cip,count);
        break;
      case 4:
        fprintf(fplist,"%08" PRIx32 " ",*(uint32_t*)cip);
        addchars(line,*(uint32_t*)cip,count);
        break;
      case 8:
        fprintf(fplist,"%016" PRIx64 " ",*(uint64_t*)cip);
        addchars(line,*(uint64_t*)cip,count);
        break;
      default:
        assert(0);
      } /* case */
      count=(count+1) % cells_per_line;
      cip+=pc_cellsize;
    } /* while */
    if (strlen(line)>0)
      fprintf(fplist,"\t; %s\n",line);
  } /* if */

  /* done with disassembly */
  free(code);
  label_deletall();
  return true;
}

static bool debug_tables(FILE *fplist)
{
  fprintf(fplist,"\n\nDEBUG INFORMATION\n");

  /* line table */
  if (amxdbg.hdr->lines>0 && amxdbg.linetbl!=NULL) {
    fprintf(fplist,"\nLines\tAddress\t\tLine number\tFilename\n");
    for (long i=0; i<amxdbg.hdr->lines; i++) {
      long line;
      const char *filename;
      ucell address;
      dbg_GetLineInfo(&amxdbg,i,&line,&filename,&address);
      fprintf(fplist,"\t%08lx\t%ld",(unsigned long)address,(long)line);
      if (filename!=NULL)
        fprintf(fplist,"\t\t%s",filename);
      fprintf(fplist,"\n");
    } /* for */
  } /* if */

  /* symbol table */
  if (amxdbg.hdr->symbols>0 && amxdbg.symboltbl!=NULL) {
    fprintf(fplist,"\nSymbols\tAddress\t\tScope\t\t\tName\n");
    for (int i=0; i<amxdbg.hdr->symbols; i++) {
      const AMX_DBG_SYMBOL *sym;
      dbg_GetSymbolInfo(&amxdbg,i,&sym);
      assert(sym!=NULL);
      assert(sym->ident!=10);  /* 'reference to function' is documented, but does not yet exist */
      if (sym->ident==iFUNCTN) {
        fprintf(fplist,"\t%08lx\t%08lx:%08lx\t",(unsigned long)sym->codestart,
                (unsigned long)sym->codestart,(unsigned long)sym->codeend);
      } else if (sym->scope==1) {
        fprintf(fplist,"\tFRM(%08lx)\t%08lx:%08lx\t",(unsigned long)sym->address,
                (unsigned long)sym->codestart,(unsigned long)sym->codeend);
      } else if (sym->scope==2) {
        fprintf(fplist,"\tDAT(%08lx)\t%08lx:%08lx\t",(unsigned long)sym->address,
                (unsigned long)sym->codestart,(unsigned long)sym->codeend);
      } else {
        fprintf(fplist,"\tDAT(%08lx)\t\t\t\t",(unsigned long)sym->address);
      } /* if */
      if (sym->tag!=0) {
        const char *tagname;
        if (dbg_GetTagName(&amxdbg,sym->tag,&tagname)==AMX_ERR_NONE)
          fprintf(fplist,"%s: ",tagname);
      } /* if */
      fprintf(fplist,"%s",sym->name);
      if (sym->dim>0) {
        const AMX_DBG_SYMDIM *symdim;
        dbg_GetArrayDim(&amxdbg,sym,&symdim);
        for (int d=0; d<sym->dim; d++) {
          if (symdim[d].size==0)
            fprintf(fplist,"[]");
          else
            fprintf(fplist,"[%lu]",(unsigned long)symdim[d].size);
          //??? also print the tag of the index, if any
        } /* for */
      } /* if */
      fprintf(fplist,"\n");
    } /* for */
  } /* if */

  if (amxdbg.hdr->states>0 && amxdbg.statetbl!=NULL) {
    fprintf(fplist,"\nStates\tName\t\tAutomaton\tSelector\n");
    for (int index = 0; index < amxdbg.hdr->states; index++) {
      const char *statename;
      dbg_GetStateName(&amxdbg,amxdbg.statetbl[index]->state,&statename);
      int automaton_id=amxdbg.statetbl[index]->automaton;
      const char *automatonname="-";
      if (amxdbg.statetbl[index]->automaton!=0)
        dbg_GetAutomatonName(&amxdbg,automaton_id,&automatonname);
      assert(amxdbg.automatontbl!=NULL);
      uint32_t addr=amxdbg.automatontbl[automaton_id]->address;
      fprintf(fplist,"\t%s\t%s\t\t%08lx\n",statename,automatonname,(unsigned long)addr);
    }
  }

  return true;
}

static void about(const char *msg)
{
  if (msg!=NULL)
    fprintf(stderr,"Error: %s.\n\n",msg);
  else
    printf("Pawn disassembler %-25s Copyright (c) 2007-2026, CompuPhase\n\n",VERSION_STR);
  printf("Usage:   pawndisasm <filename> [options]\n\n");
  printf("Options:\n");
  printf("         -d           dump debug information\n");
  printf("         -o<name>     set base name of (disassembly) output file; use '-o-' to\n"
         "                      write output to terminal\n");
  printf("         -r<from:to>  list only the code between the two addresses. The 'from'\n"
         "                      and 'to' values must be in hexadecimal (but without a '0x'\n"
         "                      prefix).\n");
  exit(1);
}

/** parseoptions() parses the command line options.
 *
 *  \param argc       Argument count, as passed from main().
 *  \param argv       [in] Argument list, as passed from main().
 *  \param input      [out] Name of the input file (the AMX file).
 *  \param output     [out] Name of the output file.
 *  \param namesz     Maximum size of both the `input` and `output` parameters.
 *  \param dbgtables  [out] Set to `true` when the debug tables must be dumped.
 *  \param addr_from  [out] Code address of the first line to be listed on the
 *                    output.
 *  \param addr_to    [out] Code address of the last line to be listed on the
 *                    output.
 */
static void parseoptions(int argc,char *argv[],char *input,char *output,size_t namesz,
                         bool *dbgtables,uint32_t *addr_from,uint32_t *addr_to)
{
  assert(argc>0);
  assert(argv!=NULL);
  assert(input!=NULL);
  assert(output!=NULL);
  assert(namesz>0);
  assert(dbgtables!=NULL);
  assert(addr_from!=NULL);
  assert(addr_to!=NULL);

  if (argc==1)
    about(NULL);

  *input='\0';        /* preset */
  *output='\0';
  *dbgtables=false;
  *addr_from=0U;
  *addr_to=0xffffffffU;

  for (int arg=1; arg<argc; arg++) {
    bool isoption;
#   if DIRSEP_CHAR=='/'
      isoption= argv[arg][0]=='-';
#   else
      isoption= argv[arg][0]=='/' || argv[arg][0]=='-';
#   endif
    char *ptr;
    if (isoption) {
      switch (argv[arg][1]) {
      case 'd':
        *dbgtables=true;
        break;
      case 'o':
        if (*output=='\0')
          strlcpy(output,argv[arg]+2,namesz);
        else
          about("multiple output files");
        break;
      case 'r':
        *addr_from=strtoul(argv[arg]+2,&ptr,16);
        if (*ptr==':')
          *addr_to=strtoul(ptr+1,NULL,16);
        break;
      case '?':
      case 'h':
        about(NULL);
        break;
      default:
        about("unknown or unsupported option");
      } /* switch */
    } else {
      if (*input=='\0')
        strlcpy(input,argv[arg],namesz);
      else
        about("multiple input files, or unrecognized option");
    } /* if */
  } /* for */

  if (*input=='\0')
    about("input file missing");

  if (*output=='\0') {
    char *ptr;
    strlcpy(output,input,namesz);
    if ((ptr=strrchr(output,'.'))!=NULL && strpbrk(ptr,"\\/:")==NULL)
      *ptr='\0';                        /* erase existing extension */
    strlcat(output,".lst",namesz);     /* append new extension */
  } /* if */
}

int main(int argc,char *argv[])
{
  char amxname[_MAX_PATH],lstname[_MAX_PATH];
  bool dumpdbg;
  uint32_t addr_from,addr_to;
  parseoptions(argc,argv,amxname,lstname,_MAX_PATH,&dumpdbg,&addr_from,&addr_to);
  if ((fpamx=fopen(amxname,"rb"))==NULL) {
    fprintf(stderr,"Error: unable to open input file \"%s\"\n",amxname);
    return 1;
  } /* if */
  FILE *fplist= (strcmp(lstname,"-")==0) ? stdout : fopen(lstname,"wt");
  if (fplist==NULL) {
    fprintf(stderr,"Error: unable to create output file \"%s\"\n",lstname);
    fclose(fpamx);
    return 1;
  } /* if */

  /* load header */
  if (fread(&amxhdr,sizeof amxhdr,1,fpamx)!=1) {
    fprintf(stderr,"Error: not a valid AMX file: \"%s\" is too short\n",amxname);
    fclose(fpamx);
    fclose_not_stdout(fplist);
    return 1;
  } /* if */
  if (amxhdr.magic==AMX_MAGIC_16) {
    pc_cellsize=2;
    pc_cellmask=0xffffU;
    pc_cellmask_p=0xffU;
  } else if (amxhdr.magic==AMX_MAGIC_32) {
    pc_cellsize=4;
    pc_cellmask=0xffffffffLU;
    pc_cellmask_p=0xffffLU;
  } else if (amxhdr.magic==AMX_MAGIC_64) {
    pc_cellsize=8;
    pc_cellmask=(ucell)0xffffffffffffffffLLU;
    pc_cellmask_p=0xffffffffLU;
  } else {
    fprintf(stderr,"Error: not a valid AMX file\n");
    fclose(fpamx);
    fclose_not_stdout(fplist);
    return 1;
  } /* if */
  if (amxhdr.flags & AMX_FLAG_CRYPT) {
    fprintf(stderr,"Error: this is an encrypted script. Encrypted scripts cannot be disassembled.\n");
    fclose(fpamx);
    fclose_not_stdout(fplist);
    return 1;
  } /* if */

  /* The offsets in the header have not been checked against each other or
   * against the file, and everything below is driven by them: the block that is
   * allocated is hea-cod, while the loops walk dat-cod of it, so dat>hea alone
   * is enough to run off the end. Check them once, here.
   */
  fseek(fpamx,0,SEEK_END);
  long filesize=ftell(fpamx);
  assert(filesize>0); /* file header was already read, so file size check should succeed */
  if (amxhdr.cod<(int32_t)sizeof amxhdr || amxhdr.cod>amxhdr.dat
      || amxhdr.dat>amxhdr.hea || amxhdr.hea>amxhdr.stp
      || amxhdr.hea>filesize || (amxhdr.hea-amxhdr.cod)%pc_cellsize!=0
      || (amxhdr.dat-amxhdr.cod)%pc_cellsize!=0
      /* the tables are read through these, and "natives - publics" is
       * subtracted to count the entries of the first one
       */
      || amxhdr.publics<(int32_t)sizeof amxhdr || amxhdr.publics>amxhdr.natives
      || amxhdr.natives>amxhdr.libraries || amxhdr.libraries>amxhdr.pubvars
      || amxhdr.pubvars>amxhdr.tags || amxhdr.tags>amxhdr.nametable
      || amxhdr.nametable>filesize)
  {
    fprintf(stderr,"Error: not a valid AMX file: the header offsets are inconsistent\n");
    fclose(fpamx);
    fclose_not_stdout(fplist);
    return 1;
  } /* if */

  /* read the debug information */
  if (dbg_LoadInfo(&amxdbg,fpamx)!=AMX_ERR_NONE) {
    memset(&amxdbg,0,sizeof amxdbg);
    if (dumpdbg)
      fprintf(stderr,"No debug information present\n");
  } /* if */

  /* dump disassembly */
  bool result=disassemble(fpamx,fplist,addr_from,addr_to);

  /* dump debug tables */
  if (result && dumpdbg && amxdbg.hdr!=NULL)
    result=debug_tables(fplist);

  dbg_FreeInfo(&amxdbg);
  fclose(fpamx);
  fclose_not_stdout(fplist);
# if defined FORTIFY
    Fortify_ListAllMemory();
# endif
  return result ? EXIT_SUCCESS : EXIT_FAILURE;
}

