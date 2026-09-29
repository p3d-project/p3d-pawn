/*  Pawn debugger interface
 *
 *  Support functions for debugger applications
 *
 *  Copyright (c) 2005-2026, CompuPhase
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
 *  Version: $Id: amxdbg.c 7701 2026-09-29 11:09:02Z thiadmer $
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined FORTIFY
# include <alloc/Fortify.h>
#endif
#if !defined PAWN_CELL_SIZE
# define PAWN_CELL_SIZE 64 /* by default, maximum cell size = 64-bit */
#endif
#include "osdefs.h"     /* for _MAX_PATH */
#include "amx.h"
#include "amxdbg.h"


int AMXAPI dbg_FreeInfo(AMX_DBG *amxdbg)
{
  assert(amxdbg != NULL);
  if (amxdbg->hdr != NULL)
    free(amxdbg->hdr);
  if (amxdbg->filetbl != NULL)
    free(amxdbg->filetbl);
  if (amxdbg->symboltbl != NULL)
    free(amxdbg->symboltbl);
  if (amxdbg->tagtbl != NULL)
    free(amxdbg->tagtbl);
  if (amxdbg->automatontbl != NULL)
    free(amxdbg->automatontbl);
  if (amxdbg->statetbl != NULL)
    free(amxdbg->statetbl);
  memset(amxdbg, 0, sizeof(AMX_DBG));
  return AMX_ERR_NONE;
}

int AMXAPI dbg_LoadInfo(AMX_DBG *amxdbg, FILE *fp)
{
  assert(fp != NULL);
  assert(amxdbg != NULL);
  memset(amxdbg, 0, sizeof(AMX_DBG));

  AMX_HEADER amxhdr = { 0 };
  fseek(fp, 0L, SEEK_SET);
  fread(&amxhdr, sizeof amxhdr, 1, fp);
  #if BYTE_ORDER==BIG_ENDIAN
    amx_Align32((uint32_t*)&amxhdr.size);
    amx_Align16(&amxhdr.magic);
    amx_Align16(&amxhdr.flags);
  #endif
  if (amxhdr.magic==AMX_MAGIC_16)
    amxdbg->cellsize=2;
  else if (amxhdr.magic==AMX_MAGIC_32)
    amxdbg->cellsize=4;
  else if (amxhdr.magic==AMX_MAGIC_64)
    amxdbg->cellsize=8;
  else
    return AMX_ERR_FORMAT;
  if ((amxhdr.flags & AMX_FLAG_DEBUG) == 0)
    return AMX_ERR_DEBUG;

  AMX_DBG_HDR dbghdr = { 0 };
  fseek(fp, amxhdr.size, SEEK_SET);
  fread(&dbghdr, sizeof(AMX_DBG_HDR), 1, fp);

  #if BYTE_ORDER==BIG_ENDIAN
    amx_Align32((uint32_t*)&dbghdr.size);
    amx_Align16(&dbghdr.magic);
    amx_Align16(&dbghdr.files);
    amx_Align16(&dbghdr.lines);
    amx_Align16(&dbghdr.symbols);
    amx_Align16(&dbghdr.tags);
    amx_Align16(&dbghdr.automatons);
    amx_Align16(&dbghdr.states);
  #endif
  if (dbghdr.magic != AMX_DBG_MAGIC)
    return AMX_ERR_FORMAT;

  /* allocate all memory */
  amxdbg->hdr = (AMX_DBG_HDR*)malloc((size_t)dbghdr.size);
  if (dbghdr.files > 0)
    amxdbg->filetbl = (AMX_DBG_FILE**)malloc(dbghdr.files * sizeof(AMX_DBG_FILE*));
  if (dbghdr.symbols > 0)
    amxdbg->symboltbl = (AMX_DBG_SYMBOL**)malloc(dbghdr.symbols * sizeof(AMX_DBG_SYMBOL*));
  if (dbghdr.tags > 0)
    amxdbg->tagtbl = (AMX_DBG_TAG**)malloc(dbghdr.tags * sizeof(AMX_DBG_TAG*));
  if (dbghdr.automatons > 0)
    amxdbg->automatontbl = (AMX_DBG_AUTOMATON**)malloc(dbghdr.automatons * sizeof(AMX_DBG_AUTOMATON*));
  if (dbghdr.states > 0)
    amxdbg->statetbl = (AMX_DBG_STATE**)malloc(dbghdr.states * sizeof(AMX_DBG_STATE*));
  if (amxdbg->hdr == NULL
      || (dbghdr.files > 0 && amxdbg->filetbl == NULL)
      || (dbghdr.symbols > 0 && amxdbg->symboltbl == NULL)
      || (dbghdr.tags > 0 && amxdbg->tagtbl == NULL)
      || (dbghdr.states > 0 && amxdbg->statetbl == NULL)
      || (dbghdr.automatons > 0 && amxdbg->automatontbl == NULL))
  {
    dbg_FreeInfo(amxdbg);
    return AMX_ERR_MEMORY;
  } /* if */

  /* load the entire symbolic information block into memory */
  memcpy(amxdbg->hdr, &dbghdr, sizeof dbghdr);
  fread(amxdbg->hdr + 1, 1, (size_t)(dbghdr.size - sizeof dbghdr), fp);

  /* run through the file, fix alignment issues and set up table pointers */
  unsigned char *ptr = (unsigned char *)(amxdbg->hdr + 1);

  /* file table */
  for (int index = 0; index < dbghdr.files; index++) {
    assert(amxdbg->filetbl != NULL);
    amxdbg->filetbl[index] = (AMX_DBG_FILE *)ptr;
    #if BYTE_ORDER==BIG_ENDIAN
      amx_Align32(&amxdbg->filetbl[index]->address);
    #endif
    for (ptr = ptr + sizeof(AMX_DBG_FILE); *ptr != '\0'; ptr++)
      /* nothing */;
    ptr++;              /* skip '\0' too */
  } /* for */

  /* line table */
  amxdbg->linetbl = (AMX_DBG_LINE*)ptr;
  #if BYTE_ORDER==BIG_ENDIAN
    for (int index = 0; index < dbghdr.lines; index++) {
      amx_Align32(&amxdbg->linetbl[index].address);
      amx_Align32((uint32_t*)&amxdbg->linetbl[index].line);
    } /* for */
  #endif
  ptr += dbghdr.lines * sizeof(AMX_DBG_LINE);

  /* symbol table (plus index tags) */
  for (int index = 0; index < dbghdr.symbols; index++) {
    assert(amxdbg->symboltbl != NULL);
    amxdbg->symboltbl[index] = (AMX_DBG_SYMBOL *)ptr;
    #if BYTE_ORDER==BIG_ENDIAN
      amx_Align32(&amxdbg->symboltbl[index]->address);
      amx_Align16((uint16_t*)&amxdbg->symboltbl[index]->tag);
      amx_Align32(&amxdbg->symboltbl[index]->codestart);
      amx_Align32(&amxdbg->symboltbl[index]->codeend);
      amx_Align16((uint16_t*)&amxdbg->symboltbl[index]->dim);
    #endif
    for (ptr = ptr + sizeof(AMX_DBG_SYMBOL); *ptr != '\0'; ptr++)
      /* nothing */;
    ptr++;              /* skip '\0' too */
    for (int dim = 0; dim < amxdbg->symboltbl[index]->dim; dim++) {
      #if BYTE_ORDER==BIG_ENDIAN
        AMX_DBG_SYMDIM *symdim = (AMX_DBG_SYMDIM *)ptr;
        amx_Align16((uint16_t*)&symdim->tag);
        amx_Align32(&symdim->size);
      #endif
      ptr += sizeof(AMX_DBG_SYMDIM);
    } /* for */
  } /* for */

  /* tag name table */
  for (int index = 0; index < dbghdr.tags; index++) {
    assert(amxdbg->tagtbl != NULL);
    amxdbg->tagtbl[index] = (AMX_DBG_TAG *)ptr;
    #if BYTE_ORDER==BIG_ENDIAN
      amx_Align16(&amxdbg->tagtbl[index]->tag);
    #endif
    for (ptr = ptr + sizeof(AMX_DBG_TAG) - 1; *ptr != '\0'; ptr++)
      /* nothing */;
    ptr++;              /* skip '\0' too */
  } /* for */

  /* automaton name table */
  for (int index = 0; index < dbghdr.automatons; index++) {
    assert(amxdbg->automatontbl != NULL);
    amxdbg->automatontbl[index] = (AMX_DBG_AUTOMATON *)ptr;
    #if BYTE_ORDER==BIG_ENDIAN
      amx_Align16(&amxdbg->automatontbl[index]->automaton);
      amx_Align32(&amxdbg->automatontbl[index]->address);
    #endif
    for (ptr = ptr + sizeof(AMX_DBG_AUTOMATON) - 1; *ptr != '\0'; ptr++)
      /* nothing */;
    ptr++;              /* skip '\0' too */
  } /* for */

  /* state name table */
  for (int index = 0; index < dbghdr.states; index++) {
    assert(amxdbg->statetbl != NULL);
    amxdbg->statetbl[index] = (AMX_DBG_STATE *)ptr;
    #if BYTE_ORDER==BIG_ENDIAN
      amx_Align16(&amxdbg->statetbl[index]->state);
      amx_Align16(&amxdbg->automatontbl[index]->automaton);
    #endif
    for (ptr = ptr + sizeof(AMX_DBG_STATE) - 1; *ptr != '\0'; ptr++)
      /* nothing */;
    ptr++;              /* skip '\0' too */
  } /* for */

  return AMX_ERR_NONE;
}

/** dbg_LinearAddress() returns the linear address that matches the given
 *  relative address for the current overlay.
 *
 *  \param amx            [in] The AMX structure.
 *  \param relative_addr  The code address inside the overlay region.
 *  \param linear_addr    [out] The linear address is relative to the code
 *                        section, as if the code section were a single block.
 *
 *  \return `AMX_ERR_NONE` on success, an error code on error.
 *
 *  \note Linearizing addresses is only needed for debugging hooks when overlays
 *        are active. On a non-overlay file, dbg_LinearAddress() returns the
 *        same address as its input.
 */
int AMXAPI dbg_LinearAddress(const AMX *amx, ucell relative_addr, ucell *linear_addr)
{
  AMX_HEADER *hdr;

  assert(amx!=NULL);
  hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(linear_addr!=NULL);
  if ((hdr->flags & AMX_FLAG_OVERLAY)==0) {
    *linear_addr=relative_addr;
  } else {
    AMX_OVERLAYINFO *tbl;
    assert(hdr->overlays!=0 && hdr->overlays!=hdr->nametable);
    if ((size_t)amx->ovl_index >= (hdr->nametable-hdr->overlays)/sizeof(AMX_OVERLAYINFO))
      return AMX_ERR_OVERLAY;
    tbl=(AMX_OVERLAYINFO*)(amx->base+hdr->overlays)+amx->ovl_index;
    *linear_addr=tbl->offset+relative_addr;
  } /* if */
  return AMX_ERR_NONE;
}

/** dbg_LookupFile() finds the source file a code address is in.
 *
 *  \param amxdbg       [in] The debug info structure.
 *  \param address      The code address.
 *  \param filename     [out] A pointer to the name of the source file.
 *
 *  \return `AMX_ERR_NONE` on success, an error code on error. If the address is
 *          outside any source file, `AMX_ERR_NOTFOUND` is returned.
 */
int AMXAPI dbg_LookupFile(const AMX_DBG *amxdbg, ucell address, const char **filename)
{
  assert(amxdbg != NULL);
  assert(filename != NULL);
  *filename = NULL;
  /* this is a simple linear look-up; a binary search would be possible too */
  int index;
  for (index = 0; index < amxdbg->hdr->files && amxdbg->filetbl[index]->address <= address; index++)
    /* nothing */;
  /* reset for overrun */
  if (--index < 0)
    return AMX_ERR_NOTFOUND;

  *filename = amxdbg->filetbl[index]->name;
  return AMX_ERR_NONE;
}

/** dbg_LookupLineNumber() finds the source line number matching a code address.
 *
 *  \param amxdbg       [in] The debug info structure.
 *  \param address      The code address.
 *  \param line         [out] The line number. This line number starts at an
 *                      address that equal to, or lower than the address passed
 *                      to this function (parameter `address`).
 *
 *  \return `AMX_ERR_NONE` on success, an error code on error. If no line number
 *          matches the address, `AMX_ERR_NOTFOUND` is returned.
 */
int AMXAPI dbg_LookupLineNumber(const AMX_DBG *amxdbg, ucell address, long *line)
{
  assert(amxdbg != NULL);
  assert(line != NULL);
  *line = 0;
  /* this is a simple linear look-up; a binary search would be possible too */
  int index;
  for (index = 0; index < amxdbg->hdr->lines && amxdbg->linetbl[index].address <= address; index++)
    /* nothing */;
  /* reset for overrun */
  if (--index < 0)
    return AMX_ERR_NOTFOUND;

  *line = (long)amxdbg->linetbl[index].line;
  return AMX_ERR_NONE;
}

/** dbg_LookupFunction() finds the function a code address is in. It can be
 *  used for stack walking, and for stepping through a function while stepping
 *  over sub-functions.
 *
 *  \param amxdbg       [in] The debug info structure.
 *  \param address      The code address.
 *  \param funcname     [out] A pointer to the name of the function.
 *
 *  \return `AMX_ERR_NONE` on success, an error code on error. If the address is
 *          outside any function, `AMX_ERR_NOTFOUND` is returned.
 */
int AMXAPI dbg_LookupFunction(const AMX_DBG *amxdbg, ucell address, const char **funcname)
{
  assert(amxdbg != NULL);
  assert(funcname != NULL);
  *funcname = NULL;
  int index;
  for (index = 0; index < amxdbg->hdr->symbols; index++) {
    if (amxdbg->symboltbl[index]->ident == iFUNCTN
        && amxdbg->symboltbl[index]->codestart <= address
        && amxdbg->symboltbl[index]->codeend > address)
      break;
  } /* for */
  if (index >= amxdbg->hdr->symbols)
    return AMX_ERR_NOTFOUND;

  *funcname = amxdbg->symboltbl[index]->name;
  return AMX_ERR_NONE;
}

/** dbg_GetTagName() returns the name of a tag identified by an xID number.
 *
 *  \param amxdbg       [in] The debug info structure.
 *  \param tag          The tag id (should be non-zero).
 *  \param name         [out] A pointer to the name of the tag.
 *
 *  \return `AMX_ERR_NONE` on success, an error code on error.
 */
int AMXAPI dbg_GetTagName(const AMX_DBG *amxdbg, int tag, const char **name)
{
  int index;

  assert(amxdbg != NULL);
  assert(name != NULL);
  *name = NULL;
  for (index = 0; index < amxdbg->hdr->tags && amxdbg->tagtbl[index]->tag != tag; index++)
    /* nothing */;
  if (index >= amxdbg->hdr->tags)
    return AMX_ERR_NOTFOUND;

  *name = amxdbg->tagtbl[index]->name;
  return AMX_ERR_NONE;
}

/** dbg_GetAutomatonName() returns the name of an automaton identified by an ID
 *  number.
 *
 *  \param amxdbg       [in] The debug info structure.
 *  \param automaton    The automaton id (should be non-zero; automaton 0 is the
 *                      anonymous automaton).
 *  \param name         [out] A pointer to the name of the automaton.
 *
 *  \return `AMX_ERR_NONE` on success, an error code on error.
 */
int AMXAPI dbg_GetAutomatonName(const AMX_DBG *amxdbg, int automaton, const char **name)
{
  int index;

  assert(amxdbg != NULL);
  assert(name != NULL);
  *name = NULL;
  for (index = 0; index < amxdbg->hdr->automatons && amxdbg->automatontbl[index]->automaton != automaton; index++)
    /* nothing */;
  if (index >= amxdbg->hdr->automatons)
    return AMX_ERR_NOTFOUND;

  *name = amxdbg->automatontbl[index]->name;
  return AMX_ERR_NONE;
}

/** dbg_GetStateName() returns the name of a state identified by an ID number.
 *
 *  \param amxdbg       [in] The debug info structure.
 *  \param state        The state id (should be non-zero).
 *  \param name         [out] A pointer to the name of the state.
 *
 *  \return `AMX_ERR_NONE` on success, an error code on error.
 */
int AMXAPI dbg_GetStateName(const AMX_DBG *amxdbg, int state, const char **name)
{
  int index;

  assert(amxdbg != NULL);
  assert(name != NULL);
  *name = NULL;
  for (index = 0; index < amxdbg->hdr->states && amxdbg->statetbl[index]->state != state; index++)
    /* nothing */;
  if (index >= amxdbg->hdr->states)
    return AMX_ERR_NOTFOUND;

  *name = amxdbg->statetbl[index]->name;
  return AMX_ERR_NONE;
}

/** dbg_LookupLineAddress() finds a suitable 'breakpoint address' close to the
 *  indicated line (and in the specified file).
 *
 *  \param amxdbg       [in] The debug info structure.
 *  \param line         The source line number.
 *  \param filename     [in] The source file name.
 *  \param address      [out] The code address matching the line number in the
 *                      specified file. If the passed-in line number is not
 *                      listed in the `linetbl` table of the debug info, the
 *                      address is moved up to the next line that table (which
 *                      is the next line that a breakpoint can be set on).
 *                      You can use function dbg_LookupLine() to find out at
 *                      which precise line the returned address is for.
 *
 *  \return `AMX_ERR_NONE` on success, an error code on error.
 *
 *  \note The filename comparison is strict (case sensitive and path sensitive);
 *        it must be identical to the filename in the `filetbl` of the `AMX_DBG`
 *        structure.
 */
int AMXAPI dbg_LookupLineAddress(const AMX_DBG *amxdbg, long line, const char *filename, ucell *address)
{
  int file, index;
  ucell bottomaddr,topaddr;

  assert(amxdbg != NULL);
  assert(filename != NULL);
  assert(address != NULL);
  *address = 0;

  index = 0;
  for (file = 0; file < amxdbg->hdr->files; file++) {
    /* find the (next) mathing instance of the file */
    if (strcmp(amxdbg->filetbl[file]->name, filename) != 0)
      continue;
    /* get address range for the current file */
    bottomaddr = amxdbg->filetbl[file]->address;
    topaddr = (file + 1 < amxdbg->hdr->files) ? amxdbg->filetbl[file+1]->address : (ucell)(cell)-1;
    /* go to the starting address in the line table */
    while (index < amxdbg->hdr->lines && amxdbg->linetbl[index].address < bottomaddr)
      index++;
    /* browse until the line is found or until the top address is exceeded */
    while (index < amxdbg->hdr->lines
           && amxdbg->linetbl[index].line < line
           && amxdbg->linetbl[index].address < topaddr)
      index++;
    if (index >= amxdbg->hdr->lines)
      return AMX_ERR_NOTFOUND;
    if (amxdbg->linetbl[index].line >= line)
      break;
    /* if not found (and the line table is not yet exceeded) try the next
     * instance of the same file (a file may appear twice in the file table)
     */
  } /* for */

  if (strcmp(amxdbg->filetbl[file]->name, filename) != 0)
    return AMX_ERR_NOTFOUND;

  assert(index < amxdbg->hdr->lines);
  *address = amxdbg->linetbl[index].address;
  return AMX_ERR_NONE;
}

/** dbg_LookupFunctionAddress() finds a suitable 'breakpoint address' for the
 *  specified function.
 *
 *  \param amxdbg       [in] The debug info structure.
 *  \param funcname     [in] The function name.
 *  \param filename     [in] The source file name (in which to locate the
 *                      function).
 *  \param address      [out] The code address for the first 'code line' in the
 *                      function. That is, The address is moved up to the first
 *                      'break-able' line in the function. You can use function
 *                      dbg_LookupLine() to find out at which precise line the
 *                      returned address is for.
 *
 *  \return `AMX_ERR_NONE` on success, an error code on error.
 *
 *  \note The comparison of the file and function names are is strict (case
 *        sensitive, and filename is path sensitive too).
 */
int AMXAPI dbg_LookupFunctionAddress(const AMX_DBG *amxdbg, const char *funcname, const char *filename, ucell *address)
{
  int index;
  const char *tgtfile;
  ucell funcaddr;

  assert(amxdbg != NULL);
  assert(funcname != NULL);
  assert(filename != NULL);
  assert(address != NULL);
  *address = 0;

  index = 0;
  for ( ;; ) {
    int err;
    /* find (next) matching function */
    while (index < amxdbg->hdr->symbols
           && (amxdbg->symboltbl[index]->ident != iFUNCTN || strcmp(amxdbg->symboltbl[index]->name, funcname) != 0))
      index++;
    if (index >= amxdbg->hdr->symbols)
      return AMX_ERR_NOTFOUND;
    /* verify that this line falls in the appropriate file */
    err = dbg_LookupFile(amxdbg, amxdbg->symboltbl[index]->address, &tgtfile);
    if (err == AMX_ERR_NONE && strcmp(filename, tgtfile) == 0)
      break;
    index++;            /* line is the wrong file, search further */
  } /* for */

  /* now find the first line in the function where we can "break" on */
  assert(index < amxdbg->hdr->symbols);
  funcaddr = amxdbg->symboltbl[index]->address;
  for (index = 0; index < amxdbg->hdr->lines && amxdbg->linetbl[index].address < funcaddr; index++)
    /* nothing */;

  if (index >= amxdbg->hdr->lines)
    return AMX_ERR_NOTFOUND;
  *address = amxdbg->linetbl[index].address;

  return AMX_ERR_NONE;
}

/** dbg_GetVariable() returns a pointer to the general symbol information,
 *  but it filters out functions (so it retrieves only variables).
 *
 *  \param amxdbg       [in] The debug info structure.
 *  \param symname      [in] The name of the variable. The symbol name lookup is
 *                      case sensitive.
 *  \param scopeaddr    The code address to filter on. Only variables that are
 *                      in scope at this address are considered.
 *  \param sym          [out] The information structure for the symbol.
 *
 *  \return `AMX_ERR_NONE` on success, an error code on error.
 *
 *  \note When there are multiple symbols with the same name that are in scope
 *        (that is, when local variables shadow other variables at a lower
 *        scope), the symbol with the smallest code scope is returned (in
 *        practice, that is the variable at the deepest nesting level).
 *
 *  \note The information returned through `sym` parameter lacks dimension
 *        information for arrays. See `dbg_GetArrayDim()` to complete the
 *        information if the symbol is an array.
 */
int AMXAPI dbg_GetVariable(const AMX_DBG *amxdbg, const char *symname, ucell scopeaddr, const AMX_DBG_SYMBOL **sym)
{
  ucell codestart,codeend;
  int index;

  assert(amxdbg != NULL);
  assert(symname != NULL);
  assert(sym != NULL);
  *sym = NULL;

  codestart = codeend = 0;
  index = 0;
  for ( ;; ) {
    /* find (next) matching variable */
    while (index < amxdbg->hdr->symbols
           && (amxdbg->symboltbl[index]->ident == iFUNCTN || strcmp(amxdbg->symboltbl[index]->name, symname) != 0)
           && (amxdbg->symboltbl[index]->codestart > scopeaddr || amxdbg->symboltbl[index]->codeend < scopeaddr))
      index++;
    if (index >= amxdbg->hdr->symbols)
      break;
    /* check the range, keep a pointer to the symbol with the smallest range */
    if (strcmp(amxdbg->symboltbl[index]->name, symname) == 0
        && (codestart == 0 && codeend == 0
            || amxdbg->symboltbl[index]->codestart >= codestart && amxdbg->symboltbl[index]->codeend <= codeend))
    {
      *sym = amxdbg->symboltbl[index];
      codestart = amxdbg->symboltbl[index]->codestart;
      codeend = amxdbg->symboltbl[index]->codeend;
    } /* if */
    index++;
  } /* for */

  return (*sym == NULL) ? AMX_ERR_NOTFOUND : AMX_ERR_NONE;
}

/** dbg_GetArrayDim() retrieves a pointer to the array dimensions structures of
 *  an array symbol.
 *
 *  \param amxdbg       [in] The debug info structure.
 *  \param sym          [in] The information structure for the symbol, which was
 *                      retrieved with `dbg_GetVariable()`.
 *  \param symdim       [out] A pointer to the first dimension of the array.
 *
 *  \return `AMX_ERR_NONE` on success, an error code on error.
 *
 *  \note In case of a multi-dimensional array, the returned value (through
 *        parameter `symdim`) points to an array of `AMX_DBG_SYMDIM` structures.
 *        The number of these structures can be read from the `sym` parameter.
 */
int AMXAPI dbg_GetArrayDim(const AMX_DBG *amxdbg, const AMX_DBG_SYMBOL *sym, const AMX_DBG_SYMDIM **symdim)
{
  const char *ptr;

  (void)amxdbg;
  assert(sym != NULL);
  assert(symdim != NULL);
  *symdim = NULL;

  if (sym->ident != iARRAY && sym->ident != iREFARRAY)
    return AMX_ERR_PARAMS;
  assert(sym->dim > 0); /* array must have at least one dimension */

  /* find the end of the symbol name */
  for (ptr = sym->name; *ptr != '\0'; ptr++)
    /* nothing */;
  *symdim = (AMX_DBG_SYMDIM *)(ptr + 1);/* skip '\0' too */

  return AMX_ERR_NONE;
}

/** dbg_GetLineInfo() gets the information on a line in the debug information.
 *
 *  \param amxdbg       [in] The debug info structure.
 *  \param index        The sequence number of the line record to return;
 *                      use 0 for the first line.
 *  \param line         [out] The line number (1-based) in the record.
 *  \param filename     [out] A pointer to the filename for this line record.
 *  \param address      [out] The code address for the line.
 *
 *  \return `AMX_ERR_NONE` on success, an error code on error.
 */
int AMXAPI dbg_GetLineInfo(const AMX_DBG *amxdbg, int index, long *line, const char **filename, ucell *address)
{
  assert(amxdbg != NULL);
  assert(index>=0);
  assert(line != NULL);
  assert(filename != NULL);
  assert(address != NULL);
  *line = 0;
  *filename = NULL;
  *address = 0;

  if (index<0 || index >= amxdbg->hdr->lines)
    return AMX_ERR_NOTFOUND;
  *line = amxdbg->linetbl[index].line;
  *address = amxdbg->linetbl[index].address;
  dbg_LookupFile(amxdbg,*address,filename);
  return AMX_ERR_NONE;
}

/** dbg_GetSymbolInfo() returns general symbol information (all kinds of
 *  symbols are returned: functions, variables, arguments)
 *
 *  \param amxdbg       [in] The debug info structure.
 *  \param index        The sequence number of the symbol record to return;
 *                      use 0 for the first symbol.
 *  \param sym          [out] A pointer into the symbolic information.
 *
 *  \return `AMX_ERR_NONE` on success, an error code on error.
 *
 *  \note For arrays, use dbg_GetArrayDim() to get information on dimensions and
 *        use dbg_GetTagName() to look up tag names.
 */
int AMXAPI dbg_GetSymbolInfo(const AMX_DBG *amxdbg, int index, const AMX_DBG_SYMBOL **sym)
{
  assert(amxdbg != NULL);
  assert(index>=0);
  assert(sym != NULL);
  *sym = NULL;

  if (index < 0 || index >= amxdbg->hdr->symbols)
    return AMX_ERR_NOTFOUND;
  *sym = amxdbg->symboltbl[index];
  return AMX_ERR_NONE;
}

