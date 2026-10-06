# amxexec_gas.S     Abstract Machine for the "Pawn" language, in GNU AS syntax
#

# Some notes:
#  * Please also look at the "history" log (below) for more notes.
#  * This file assembles with GNU AS (or GCC); see amxexec.asm for a version that
#    builds with Microsoft MASM and amxexecn.asm for a version for NASM.
#  * There are two "calling convention" issues:
#    o  The convention with which amx_exec_asm() itself is called. This is
#       currently hard-coded to __cdecl.
#    o  The convention for calling the "hook" functions (native functions and
#       the debugger callback). The default is __cdecl; it can be changed to
#       __stdcall by defining the macro STDCALL.
#  * You will need to compile the standard amx.c file with the macro AMX_ASM
#    defined. On the command line, use:
#        gcc -DAMX_ASM -opawnrun pawnrun.c amx.c amxcore.c amxcons.c amxexec_gas.S
#  * OP_CASETBL and OP_CASETBL.OVL are not implemented, but they should not occur
#    anyway.
#
#
# Copyright and license of use, please read
# -----------------------------------------
# The assembler implementation of the abstract machine for the Pawn language,
# specifically the file AMXEXEC.ASM, is copyright (c) 1998-2000 by Marc Peter.
#
# Pertaining to condition 2 of the license (see below), take note that that the
# original file has been substantially altered by Thiadmer Riemersma. (See also
# the history of changes).
#
# Permission is hereby granted, without written agreement and without paid
# license or royalty fees, to use, copy, modify, and distribute this software
# and its documentation for any purpose, subject to the following conditions:
#
# 1. The above copyright notice and this permission notice shall appear in all
#    copies or substantial portions of this software.
#
# 2. Modifications of this software that do not originate from me (Marc Peter)
#    must be explicitly mentioned in a README file or another appropriate
#    place.
#
# The use of this software as a subsystem of a larger software product is
# explicitly allowed, regardless of whether that larger product is proprietary,
# gratis or commercially available.
#
# I (Marc Peter) specifically disclaim any warranties, including, but not
# limited to, the implied warranties of merchantability and fitness for a
# particular purpose. The software is provided on an "as is" basis,
# and I have no obligation to provide maintenance, support, updates,
# enhancements or modifications.
#
# I cannot be held liable for any damage or loss of profits that results
# from the use of the software (or part thereof), or from the inability to
# use it.
#
#
#
# History (list of changes)
# -------------------------
#   18 August 2026 by Thiadmer Riemersma
#       Converted to GNU assembler syntax (AT&T syntax)
#   5 June 2017 by Thiadmer Riemersma
#       The STACK and STACK.P opcodes have been modified to set the *new* value
#       of the STK register in ALT (so that it can immediately be used with
#       STOR.I, for example).
#   1 March 2010  by Thiadmer Riemersma
#       The instruction set has been reorganized: a minimal "core" instruction
#       set was picked and three supplemental instruction sets: overlay
#       instructions, macro instructions and packed instructions.
#  14 March 2009  by Thiadmer Riemersma
#       Addition of the relocation instructions (PUSHR_xxx).
#  26 August 2007  by Thiadmer Riemersma
#       Minor clean-up; removed unneeded parameter.
#  31 May 2007  by Thiadmer Riemersma
#       Added packed opcodes
#  30 April 2007  by Thiadmer Riemersma
#       Move to position-independent code (no more relocation needed for
#       branches).
#       Removed cases for obsolete instructions.
#  14 December 2005  by Thiadmer Riemersma
#       Addition of macro instructions, to speed up instruction decoding
#  17 February 2005  by Thiadmer Riemersma
#       Addition of the BREAK opcode, removal of the older debugging opcode table.
#   6 March 2004  by Thiadmer Riemersma
#       Corrected a bug in OP_FILL, where a cell preceding the array would
#       be overwritten (zero'ed out). This bug was brought to my attention
#       by Robert Daniels.
#   2 February 2004  by Thiadmer Riemersma
#       Added checking of the return address in the RET and RETN opcodes.
#       Changed handling of LINE opcode, so that the debugger can force a
#       sleep.
#  22 December 2003  by Thiadmer Riemersma
#       Added support for the SYMTAG and SYSCALL.D opcodes
#   3 October 2003  by Thiadmer Riemersma
#       Added "non-debug" versions of various opcodes, to avoid repetitive
#       checking of the "debug" flag.
#  1 July 2002 by Thiadmer Riemersma
#       Modifications due to added fields in the AMX structure
#       RET and RETN instructions no longer check for a 0 return address
#       Stack top is now relative to the start of the data segment, to allow
#       for "cloned" AMX'es
#       The semantics of _CHKSTACK and _CHKMARGIN were inverted; now corrected
#       New opcodes
#       * OP_CALLN (but commented out, due to problems with the JIT)
#       * OP_NOP
# 27 August 2001 by Dario Sacca (DS)
#       Corrected FRM parameter in the debug hook for the OP_LINE opcode.
# 25 January 2001 by Thiadmer Riemersma (TR)
#       Implement the "sleep" functionality via the OP_HALT opcode; expand the
#       AMX structure for new required fields.
# 21 December 2000 by Thiadmer Riemersma (TR)
#       Let the AMX abort if the debug hook function returns an error code
#       on the DBG_LINE code.
# 19 October 2000 by Thiadmer Riemersma (TR)
#       Store return value of a function in the AMX structure when calling the
#       debug hook
#  9 September 2000 by Thiadmer Riemersma (TR)
#       Implemented new opcodes
#       * OP_SWAP_PRI
#       * OP_SWAP_ALT
#       * OP_PUSHADDR
# 15 July 2000 By Thiadmer Riemersma (TR)
#       Store address of function being called in a debug parameter
#       Address for OP_CALL_PRI needs to be relocated
# 15 November 1999 By Thiadmer Riemersma (TR)
#       Allow the parameter for the return value to be NULL.
# 30 September 1999 By Thiadmer Riemersma (TR)
#       Implemented _stdcall calling convention
#  1 August 1999 By Thiadmer Riemersma (TR)
#       Implemented _cdecl calling convention
# 15 June 1999   By Thiadmer Riemersma (TR)
#       Implemented new opcodes
#       * OP_SRANGE
#       * OP_JUMP_PRI
#       * OP_SWITCH
#       * OP_CASETBL
#       Referring to the "license of use" of this file (see above), please
#       note that these modifications are NOT by Marc Peter. I have marked
#       the locations of the changes with a comment line starting with "TR".
# 28 March 1999
#       Here are some more bug fixes. I (Mark Peter) found these when
#       "designing" the code sequences for the JIT compiler. The Pawn compiler
#       doesn't seem to create these opcodes, or else the bugs would have
#       turned up earlier.
#       * LREF_S_ALT: garbled EAX and didn't work as expected
#       * in LCTRL: In the case of 'lctrl 6' no. 5 would have entered an
#         infinite loop because it jumped back to it's own compare instruction
#         and not to #6.
#       * in SCTRL: FRM was updated, but not EBX
# -----

.equ    AMX_ERR_NONE,           0
.equ    AMX_ERR_EXIT,           1
.equ    AMX_ERR_ASSERT,         2
.equ    AMX_ERR_STACKERR,       3
.equ    AMX_ERR_BOUNDS,         4
.equ    AMX_ERR_MEMACCESS,      5
.equ    AMX_ERR_INVINSTR,       6
.equ    AMX_ERR_STACKLOW,       7
.equ    AMX_ERR_HEAPLOW,        8
.equ    AMX_ERR_CALLBACK,       9
.equ    AMX_ERR_NATIVE,         10
.equ    AMX_ERR_DIVIDE,         11      # for catching divide errors
.equ    AMX_ERR_SLEEP,          12

.equ    AMX_ERR_MEMORY,         16
.equ    AMX_ERR_FORMAT,         17
.equ    AMX_ERR_VERSION,        18
.equ    AMX_ERR_NOTFOUND,       19
.equ    AMX_ERR_INDEX,          20
.equ    AMX_ERR_DEBUG,          21
.equ    AMX_ERR_INIT,           22
.equ    AMX_ERR_USERDATA,       23
.equ    AMX_ERR_INIT_JIT,       24
.equ    AMX_ERR_PARAMS,         25
.equ    AMX_ERR_DOMAIN,         26
.equ    AMX_ERR_GENERAL,        27
.equ    AMX_ERR_OVERLAY,        28

.equ    AMX_FLAG_OVERLAY,       0x0001 # all function calls use overlays
.equ    AMX_FLAG_DEBUG,         0x0002 # symbolic info. available
.equ    AMX_FLAG_NOCHECKS,      0x0004 # no array bounds checking; no BREAK opcodes
.equ    AMX_FLAG_SLEEP,         0x0008 # script uses the sleep instruction (possible re-entry or power-down mode)
.equ    AMX_FLAG_CRYPT,         0x0010 # scrypt is encrypted
.equ    AMX_FLAG_DSEG_INIT,     0x0020 # data section is explicitly initialized
.equ    AMX_FLAG_SYSREQN,       0x0800 # script uses new (optimized) version of SYSREQ opcode
.equ    AMX_FLAG_NTVREG,        0x1000 # all native functions are registered
.equ    AMX_FLAG_JITC,          0x2000 # abstract machine is JIT compiled
.equ    AMX_FLAG_VERIFY,        0x4000 # busy verifying P-code
.equ    AMX_FLAG_INIT,          0x8000 # AMX has been initialized


# struc amx_s
.equ _base,       0x00
.equ _codeseg,    0x04
.equ _dataseg,    0x08
.equ _callback,   0x0c
.equ _debug,      0x10
.equ _overlay,    0x14
.equ _cip,        0x18
.equ _frm,        0x1c
.equ _hea,        0x20
.equ _hlw,        0x24
.equ _stk,        0x28
.equ _stp,        0x2c
.equ _flags,      0x30
.equ _usertags,   0x34	        # AMX_USERNUM = 4 (#define'd in amx.h)
.equ _userdata,   0x44 	        # AMX_USERNUM = 4 (#define'd in amx.h)
.equ _error,      0x54
.equ _paramcount, 0x58
.equ _pri,        0x5c
.equ _alt,        0x60
.equ _reset_stk,  0x64
.equ _reset_hea,  0x68
.equ _syscall_d,  0x6c
.equ _ovl_index,  0x70
.equ _codesize,   0x74         # memory size of the overlay or of the native code
# endstruc


.ifndef AMX_NO_PACKED_OPC
  .ifndef AMX_TOKENTHREADING
    .equ AMX_TOKENTHREADING, $1 # packed opcodes require token threading
  .endif
.endif

# create some aliases to registers that are stored on the stack
# Nota Bene: for this list to make sense, see the comment #STACKALIAS in
#       _amx_exec_run, where this same list appears as a comment.
#define STK     +36(%esp)
#define ALT     +32(%esp)
#define PRI     +28(%esp)
#define CODESIZ +24(%esp)
#define CODE    +20(%esp)
#define AMX     +16(%esp)
#define RETVAL  +12(%esp)
#define STP     +8(%esp)
#define HEA     +4(%esp)
#define FRM     +0(%esp)

# #define PUSH(v)         ( stk-=sizeof(cell), *(cell *)(data+(int)stk)=v )
.macro  _PUSH v
        mov     \v,-4(%edi,%ecx)
        sub     $4,%ecx
.endm


# #define POP(v)          ( v=*(cell *)(data+(int)stk), stk+=sizeof(cell) )
.macro  _POP v
        mov     (%edi,%ecx),\v
        add     $4,%ecx
.endm

.macro  NEXT
  .ifdef  AMX_TOKENTHREADING
        mov     (%esi),%ebp
    .ifndef AMX_NO_PACKED_OPC
        and     $0x0ff,%ebp
    .endif
        jmp     *opcodelist (,%ebp,4)
  .else
    .ifndef $AMX_NO_PACKED_OPC
        .err                    # opcode packing requires token threading
    .endif
        # direct threading
        jmp     (%esi)
  .endif
#      .align  4
.endm

.macro  JUMPREL
        add     4(%esi),%esi
.endm

.macro  GETPARAM_P reg
        # ??? verify that reg != esi
        mov     (%esi),\reg
        sar     $16,\reg        # shift-right, keeping the sign
.endm

.macro  _CHKSTACK
        cmp     STP,%ecx
        jg      err_stacklow
.endm

.macro  _CHKMARGIN
        lea     -16*4(%ecx),%ebp    # savety margin = 16 cells
        cmp     %ebp,HEA
        jg      err_stack
.endm

.macro  _CHKHEAP
        mov     AMX,%ebp
        mov     _hlw(%ebp),%ebp
        cmp     %ebp,HEA
        jl      err_heaplow
.endm

.macro  _CHKDIVIDEZERO
        or      %ebp,%ebp       # check for divide by zero
        jz      err_divide
.endm

.macro  _VERIFYADDRESS adr      # used in load.i, store.i & lidx
        cmp     STP,\adr        # error if adr >= stp
        jae     err_memaccess
        cmp     HEA,\adr        # so adr<stp, ok if adr<HEA
        jb      1f
        cmp     %ecx,\adr       # so adr<stp and adr>=HEA, ok if adr>=stk
        jb      err_memaccess
    1:
.endm

.macro  _SAVEREGS               # save the registers (that may not be
        pushal                  # __stdcall calling conventions)
.endm

.macro  _RESTOREREGS
        popal
.endm

.macro  _DROPARGS n             # remove function arguments from the stack
  .ifndef STDCALL               # (for __cdecl calling convention, so do nothing for __stdcall)
        add     \n,%esp
  .endif
.endm


.text

#---------------------------------------------------------------------------
#cell   asm_exec_run( AMX *amx, cell *retval, char *data )
#                      eax        edx           ebx
#---------------------------------------------------------------------------

    .global  _amx_exec_run
    .global  _amx_exec_run@12
_amx_exec_run@12:
_amx_exec_run:

        push    %ebx
        mov     0x08(%esp),%eax # eax = amx parameter
        mov     0x0c(%esp),%edx # edx = retval parameter
        mov     0x10(%esp),%ebx # ebx = data parameter

        push    %edi
        push    %esi
        push    %ebp

        # #STACKALIAS: store the AMX registers on the stack, so that these
        #       can be read directly instead of indirectly via the AMX data
        #       structure
        # Nota Bene 1: macros are defined above to attach symbolic "aliases"
        #       to these stack relative fields.
        # Nota Bene 2: be careful with push and pop instructions after this
        #       block, because the aliases are relative to ESP.
        sub     $4*3,%esp       # place for PRI, ALT & STK at SYSREQs
        push    _codesize(%eax) # store code size
        push    _codeseg(%eax)  # store pointer to code segment
        push    %eax            # store pointer to AMX
        push    %edx            # store address of retval
        push    _stp(%eax)      # store STP
        push    _hea(%eax)      # store HEA
        push    _frm(%eax)      # store FRM

        mov     %ebx,%edi       # get pointer to data segment
        mov     _alt(%eax),%edx # get ALT
        mov     _cip(%eax),%esi # get CIP
        mov     _stk(%eax),%ecx # get STK
        mov     _frm(%eax),%ebx # get FRM
        mov     _pri(%eax),%eax # get PRI
        add     %edi,%ebx       # relocate frame
        add     CODE,%esi       # relocate code address

        NEXT                    # start interpreting


OP_NOP:
        add     $4,%esi
        NEXT


OP_LOAD_PRI:
        mov     4(%esi),%eax
        add     $8,%esi
        mov     (%edi,%eax),%eax
        NEXT


OP_LOAD_ALT:
        mov     4(%esi),%edx
        add     $8,%esi
        mov     (%edi,%edx),%edx
        NEXT


OP_LOAD_S_PRI:
        mov     4(%esi),%eax
        add     $8,%esi
        mov     (%ebx,%eax),%eax
        NEXT


OP_LOAD_S_ALT:
        mov     4(%esi),%edx
        add     $8,%esi
        mov     (%ebx,%edx),%edx
        NEXT


OP_LREF_S_PRI:
        mov     4(%esi),%eax
        add     $8,%esi
        mov     (%ebx,%eax),%eax
        mov     (%edi,%eax),%eax
        NEXT


OP_LREF_S_ALT:
        mov     4(%esi),%edx
        add     $8,%esi
        mov     (%ebx,%edx),%edx
        mov     (%edi,%edx),%edx
        NEXT


OP_ALIGN_PRI:
        mov     $4,%ebp
        sub     4(%esi),%ebp
        add     $8,%esi
        xor     %ebp,%eax
        NEXT


OP_LCTRL:
        mov     4(%esi),%ebp
        add     $8,%esi
        cmp     $0,%ebp
        jne     lctrl_1
        mov     CODE,%eax       # 0 -> COD
        jmp     lctrl_done
    lctrl_1:
        cmp     $1,%ebp
        jne     lctrl_2
        mov     %edi,%eax       # 1 -> DAT
        jmp     lctrl_done
    lctrl_2:
        cmp     $2,%ebp
        jne     lctrl_3
        mov     HEA,%eax        # 2 -> HEA
        jmp     lctrl_done
    lctrl_3:
        cmp     $3,%ebp
        jne     lctrl_4
        mov     STP,%eax        # 3 -> STP
        jmp     lctrl_done
    lctrl_4:
        cmp     $4,%ebp
        jne     lctrl_5
        mov     %ecx,%eax       # 4 -> STK
        jmp     lctrl_done
    lctrl_5:
        cmp     $5,%ebp
        jne     lctrl_6
        mov     FRM,%eax        # 5 -> FRM
        jmp     lctrl_done
    lctrl_6:
        cmp     $6,%ebp
        jne     lctrl_done
        mov     %esi,%eax       # 6 -> CIP
        sub     CODE,%eax       # reverse relocate
    lctrl_done:
        NEXT


OP_SCTRL:
        mov     4(%esi),%ebp
        add     $8,%esi
        cmp     $2,%ebp
        jne     sctrl_4
        mov     %eax,HEA        # 2 -> HEA
        jmp     sctrl_done
    sctrl_4:
        cmp     $4,%ebp
        jne     sctrl_5
        mov     %eax,%ecx       # 4 -> STK
        jmp     sctrl_done
    sctrl_5:
        cmp     $5,%ebp
        jne     sctrl_6
        mov     %eax,%ebx       # 5 -> FRM
        mov     %eax,FRM
        add     %edi,%ebx       # relocate
    sctrl_6:
        cmp     $6,%ebp
        jne     sctrl_done
        mov     %eax,%esi       # 6 -> CIP
        add     CODE,%esi       # relocate
    sctrl_done:
        NEXT


OP_XCHG:
        add     $4,%esi
        xchg    %edx,%eax
        NEXT


OP_PUSH_PRI:
        add     $4,%esi
        _PUSH   %eax
        NEXT


OP_PUSH_ALT:
        add     $4,%esi
        _PUSH   %edx
        NEXT


OP_LOAD_I:
        add     $4,%esi
        _VERIFYADDRESS %eax
        mov     (%edi,%eax),%eax
        NEXT


OP_LODB_I:
        _VERIFYADDRESS %eax
        mov     4(%esi),%ebp
        mov     (%edi,%eax),%eax    # subject to misalignment stalls
        add     $8,%esi
        and     (lodb_and-4)(,%ebp,4),%eax
        NEXT


OP_CONST_PRI:
        mov     4(%esi),%eax
        add     $8,%esi
        NEXT


OP_CONST_ALT:
        mov     4(%esi),%edx
        add     $8,%esi
        NEXT


OP_ADDR_PRI:
        mov     4(%esi),%eax
        add     $8,%esi
        add     FRM,%eax
        NEXT


OP_ADDR_ALT:
        mov     4(%esi),%edx
        add     $8,%esi
        add     FRM,%edx
        NEXT


OP_STOR:
        mov     4(%esi),%ebp
        add     $8,%esi
        mov     %eax,(%ebp,%edi)
        NEXT


OP_STOR_S:
        mov     4(%esi),%ebp
        add     $8,%esi
        mov     %eax,(%ebp,%ebx)
        NEXT


OP_SREF_S:
        mov     4(%esi),%ebp
        add     $8,%esi
        mov     (%ebx,%ebp),%ebp
        mov     %eax,(%edi,%ebp)
        NEXT


OP_STOR_I:
        add     $4,%esi
        _VERIFYADDRESS %edx
        mov     %eax,(%edi,%edx)
        NEXT


OP_STRB_I:
        mov     4(%esi),%ebp
        add     $8,%esi
    strb_entry:
        _VERIFYADDRESS %edx
        cmp     $1,%ebp
        jne     strb_not1byte
        movb    %al,(%edi,%edx)
        NEXT
    strb_not1byte:
        cmp     $4,%ebp
        je      strb_4byte
        movw    %ax,(%edi,%edx)
        NEXT
    strb_4byte:
        mov     %eax,(%edi,%edx)
        NEXT


OP_PUSHR_PRI:
        mov     %eax,%ebp
        add     $4,%esi
        add     %edi,%ebp
        _PUSH   %ebp
        NEXT


OP_POP_PRI:
        add     $4,%esi
        _POP    %eax
        NEXT


OP_POP_ALT:
        add     $4,%esi
        _POP    %edx
        NEXT


OP_PICK:
        mov     4(%esi),%eax
        add     $8,%esi
        add     %ecx,%eax
        mov     (%edi,%eax),%eax
        NEXT


OP_STACK:
        add     4(%esi),%ecx
        mov     %ecx,%edx
        add     $8,%esi
        _CHKMARGIN
        _CHKSTACK
        NEXT


OP_HEAP:
        mov     4(%esi),%ebp
        mov     HEA,%edx
        add     $8,%esi
        add     %ebp,HEA
        _CHKMARGIN
        _CHKHEAP
        NEXT


OP_PROC:
        mov     FRM,%ebx
        add     $4,%esi
        _PUSH   %ebx
        mov     %edi,%ebx
        mov     %ecx,FRM
        add     %ecx,%ebx
        _CHKMARGIN
        NEXT


OP_RET:
        _POP    %ebx
        _POP    %esi
        cmp     CODESIZ,%esi   # verify ESI<codesiz
        jae     err_memaccess
        mov     %ebx,FRM
        add     CODE,%esi      # relocate ESI and EBI
        add     %edi,%ebx
        NEXT


OP_RETN:
        _POP    %ebx
        _POP    %esi
        cmp     CODESIZ,%esi   # verify ESI<codesiz
        jae     err_memaccess
        mov     %ebx,FRM
        add     CODE,%esi      # relocate ESI and EBI
        add     %edi,%ebx
        mov     (%edi,%ecx),%ebp
        lea     4(%ecx,%ebp),%ecx
        NEXT


OP_CALL:
        lea     8(%esi),%ebp
        JUMPREL         # add esi,[esi+4]
        sub     CODE,%ebp      # reverse relocate (before push)
        _PUSH   %ebp
        NEXT


OP_JUMP:
        JUMPREL         # add esi,[esi+4]
        NEXT


OP_JZER:
        or      %eax,%eax
        jz      OP_JUMP
        add     $8,%esi
        NEXT


OP_JNZ:
        or      %eax,%eax
        jnz     OP_JUMP
        add     $8,%esi
        NEXT


OP_SHL:
        push    %ecx
        mov     %edx,%ecx
        add     $4,%esi
        shl     %cl,%eax
        pop     %ecx
        NEXT


OP_SHR:
        push    %ecx
        mov     %edx,%ecx
        add     $4,%esi
        shr     %cl,%eax
        pop     %ecx
        NEXT


OP_SSHR:
        push    %ecx
        mov     %edx,%ecx
        add     $4,%esi
        sar     %cl,%eax
        pop     %ecx
        NEXT


OP_SHL_C_PRI:
        push    %ecx
        mov     4(%esi),%ecx
        add     $8,%esi
        shl     %cl,%eax
        pop     %ecx
        NEXT


OP_SHL_C_ALT:
        push    %ecx
        mov     4(%esi),%ecx
        add     $8,%esi
        shl     %cl,%edx
        pop     %ecx
        NEXT


OP_SMUL:
        add     $4,%esi
        push    %edx
        imul    %edx
        pop     %edx
        NEXT


OP_SDIV:
        xchg    %edx,%eax

        # Although SDIV.INV is a supplemental instruction, it is implemented
        # as a core instruction here, because SDIV is implemented in terms
        # of SDIV.INV
OP_SDIV_INV:
        mov     %edx,%ebp
        cmp     $-1,%ebp        # Dividing by -1 is a negation, and it is the one
        je      sdiv_negate     # division that overflows (idiv traps on it).
        xor     %eax,%edx       # Check signs of the operands.
        cltd
        js      sdiv_fiddle     # If the signs of the operands are different
        # we'll have to fiddle around to achieve
        # proper rounding towards minus infinity.
        _CHKDIVIDEZERO
        add     $4,%esi         # default behavior is right in the other cases
        idiv    %ebp
        NEXT

    sdiv_negate:
        neg     %eax            # The smallest cell stays itself,
        xor     %edx,%edx       # and the remainder is zero.
        add     $4,%esi
        NEXT

    sdiv_fiddle:
        _CHKDIVIDEZERO
        idiv    %ebp
        add     $4,%esi
        or      %edx,%edx
        jz      sdiv_goon       # If there's no remainder the result is correct
        add     %ebp,%edx       # else fix the result values.
        dec     %eax            # amazing, how simple this is...
    sdiv_goon:
        NEXT


OP_ADD:
        add     $4,%esi
        add     %edx,%eax
        NEXT


OP_SUB:
        neg     %eax
        add     $4,%esi
        add     %edx,%eax
        NEXT


OP_AND:
        add     $4,%esi
        and     %edx,%eax
        NEXT


OP_OR:
        add     $4,%esi
        or      %edx,%eax
        NEXT


OP_XOR:
        add     $4,%esi
        xor     %edx,%eax
        NEXT


OP_NOT:
        add     $4,%esi
        neg     %eax            # sets CF iff EAX != 0
        sbb     %eax,%eax       # EAX == -1 iff CF set (zero otherwise)
        inc     %eax            # -1 => 0 and 0 => 1
        NEXT


OP_NEG:
        add     $4,%esi
        neg     %eax
        NEXT


OP_INVERT:
        add     $4,%esi
        not     %eax
        NEXT


OP_EQ:
        add     $4,%esi
        cmp     %edx,%eax       # PRI == ALT ?
        mov     $0,%eax
        seteb   %al
        NEXT


OP_NEQ:
        add     $4,%esi
        cmp     %edx,%eax       # PRI != ALT ?
        mov     $0,%eax
        setneb  %al
        NEXT


OP_SLESS:
        add     $4,%esi
        cmp     %edx,%eax       # PRI < ALT ? (signed)
        mov     $0,%eax
        setlb   %al
        NEXT


OP_SLEQ:
        add     $4,%esi
        cmp     %edx,%eax       # PRI <= ALT ? (signed)
        mov     $0,%eax
        setleb  %al
        NEXT


OP_SGRTR:
        add     $4,%esi
        cmp     %edx,%eax       # PRI > ALT ? (signed)
        mov     $0,%eax
        setgb   %al
        NEXT


OP_SGEQ:
        add     $4,%esi
        cmp     %edx,%eax       # PRI >= ALT ? (signed)
        mov     $0,%eax
        setgeb  %al
        NEXT


OP_INC_PRI:
        add     $4,%esi
        inc     %eax
        NEXT


OP_INC_ALT:
        add     $4,%esi
        inc     %edx
        NEXT


OP_INC_I:
        add     $4,%esi
        _VERIFYADDRESS %eax
        incl    (%edi,%eax)
        NEXT


OP_DEC_PRI:
        add     $4,%esi
        dec     %eax
        NEXT


OP_DEC_ALT:
        add     $4,%esi
        dec     %edx
        NEXT


OP_DEC_I:
        add     $4,%esi
        _VERIFYADDRESS %eax
        subl    $1,(%edi,%eax)
        NEXT


OP_MOVS:
        _VERIFYADDRESS %eax    # PRI
        _VERIFYADDRESS %edx    # ALT
        mov     %eax,%ebp
        add     4(%esi),%ebp
        dec     %ebp
        _VERIFYADDRESS %ebp    # PRI + size - 1
        sub     %eax,%ebp       # EBP = size - 1
        add     %edx,%ebp
        _VERIFYADDRESS %ebp    # ALT + size - 1

        push    %ecx
        mov     4(%esi),%ecx
        add     $8,%esi
    movs_entry:
        push    %edi
        push    %esi
        lea     (%edi,%eax),%esi
        lea     (%edi,%edx),%edi

        push    %ecx
        shr     $2,%ecx
        rep     movsl
        pop     %ecx
        and     $3,%ecx
        rep     movsb

        pop     %esi
        pop     %edi
        pop     %ecx
        NEXT


OP_CMPS:
        _VERIFYADDRESS %eax    # PRI
        _VERIFYADDRESS %edx    # ALT
        mov     %eax,%ebp
        add     4(%esi),%ebp    # size in bytes
        dec     %ebp            # EBP = PRI + size - 1
        _VERIFYADDRESS %ebp     # PRI + size - 1
        sub     %eax,%ebp       # EBP = size - 1
        add     %edx,%ebp       # EBP = ALT + size - 1
        _VERIFYADDRESS %ebp     # ALT + size - 1

        push    %ecx
        mov     4(%esi),%ecx
        add     $8,%esi
    cmps_entry:
        push    %edi
        push    %esi
        lea     (%edi,%edx),%esi
        lea     (%edi,%eax),%edi

        xor     %eax,%eax
        repe    cmpsb
        je      cmps1
        sbb     %eax,%eax
        sbb     $0x0ffffffff,%eax
    cmps1:
        pop     %esi
        pop     %edi
        pop     %ecx
        NEXT


OP_FILL:
        mov     4(%esi),%ebp    # get byte count
        add     $8,%esi
    fill_entry:
        and     $0x0fffffffc,%ebp       # align to words
        jz      fill_ready
        _VERIFYADDRESS %edx     # ALT
        dec     %ebp            # EBP = size - 1
        add     %edx,%ebp       # EBP = ALT + size - 1
        _VERIFYADDRESS %ebp     # ALT + size - 1
        sub     %edx,%ebp       # restore EBP
        inc     %ebp

        push    %ecx
        push    %edi
        mov     %ebp,%ecx       # ECX = count (in bytes)
        lea     (%edi,%edx),%edi        # EDI = physical starting address
        shr     $2,%ecx # ECX = count (in DWORDS)
        rep     stosl
        pop     %edi
        pop     %ecx
    fill_ready:
        NEXT


OP_HALT:
        cmpl    $0,RETVAL
        je      halt_no_retval
        mov     RETVAL,%ebp
        mov     %eax,(%ebp)
    halt_no_retval:
        # store the complete status in the AMX
        mov     AMX,%ebp        # get amx into ebp
        mov     %eax,_pri(%ebp) # store values in AMX structure (PRI, ALT, STK, HEA, FRM, ...)
        mov     %edx,_alt(%ebp)
        mov     %ecx,_stk(%ebp)
        mov     HEA,%ecx
        mov     FRM,%ebx
        mov     %ecx,_hea(%ebp)
        mov     %ebx,_frm(%ebp) # EBX & ECX are invalid by now
        mov     4(%esi),%ebx    # EBX=parameter of the HALT opcode
        add     $8,%esi         # skip this instruction
        mov     %esi,%eax       # EAX=CIP
        sub     CODE,%eax
        mov     %eax,_cip(%ebp)
        mov     %ebx,%eax       # return the parameter of the HALT opcode
        jmp     _return


OP_BOUNDS:
        mov     4(%esi),%ebp
        add     $8,%esi
        cmp     %ebp,%eax
        ja      err_bounds      # use unsigned comparison, so <0 is >bounds
        NEXT


OP_SYSREQ:
        mov     4(%esi),%eax    # get function number
        add     $8,%esi
        mov     AMX,%ebp        # get amx into ebp
        mov     %ecx,STK        # save STK
        mov     %edx,ALT        # save ALT

        mov     %ecx,_stk(%ebp) # store values in AMX structure (STK, HEA, FRM)
        mov     HEA,%ecx
        mov     FRM,%ebx
        mov     %ecx,_hea(%ebp)
        mov     %ebx,_frm(%ebp) # ebx & ecx are invalid by now

        mov     %esi,%ebx       # also store CIP
        sub     CODE,%ebx
        mov     %ebx,_cip(%ebp)

        mov     %eax,%edx       # 2nd param: function number
        mov     %ebp,%eax       # 1st param: amx
        mov     STK,%ecx
        lea     PRI,%ebx        # 3rd param: addr. of retval
        add     %edi,%ecx       # 4th param: addr. of function parameters
        # save a few registers (it is not necessary to save them all
        # and EAX should *not* be saved because it will hold the return
        # value)
        push    %ebp
        push    %esi
        push    %edi
        # push the parameters
        push    %ecx
        push    %ebx
        push    %edx
        push    %eax
        call    *_callback(%ebp)
        _DROPARGS $0x10         # remove arguments from stack
        pop     %edi            # restore saved registers
        pop     %esi
        pop     %ebp
        cmp     $AMX_ERR_NONE,%eax
        jne     _return         # return error code, if any

        mov     PRI,%eax        # get retval into eax (PRI)
        mov     ALT,%edx        # restore ALT
        mov     FRM,%ebx
        mov     STK,%ecx        # restore STK
        add     %edi,%ebx       # restore FRM
        NEXT


OP_SWITCH:
        push    %ecx
        mov     %esi,%ebp       # EBP = CIP
        add     4(%esi),%ebp    # EBP = offset of the casetable
        add     $4,%ebp         # skip the "OP_CASETBL" opcode
        mov     (%ebp),%ecx     # ECX = number of records
        mov     %ebp,%esi       # ESI = address of first record
        add     4(%ebp),%esi    # preset ESI to "none-matched" case
    op_switch_loop:
        or      %ecx,%ecx       # number of records == 0?
        jz      op_switch_end   # yes, no more records, exit loop
        add     $8,%ebp         # skip previous record
        dec     %ecx            # already decrement cases to do
        cmp     (%ebp),%eax     # PRI == case label?
        jne     op_switch_loop  # no, continue loop
        mov     %ebp,%esi       # yes, get jump address and exit loop
        add     4(%ebp),%esi
    op_switch_end:
        pop     %ecx
        NEXT


OP_SWAP_PRI:
        mov     (%edi,%ecx),%ebp
        add     $4,%esi
        mov     %eax,(%edi,%ecx)
        mov     %ebp,%eax
        NEXT


OP_SWAP_ALT:
        mov     (%edi,%ecx),%ebp
        add     $4,%esi
        mov     %edx,(%edi,%ecx)
        mov     %ebp,%edx
        NEXT


OP_BREAK:
        mov     AMX,%ebp        # get amx into ebp
        add     $4,%esi
        cmpl    $0,_debug(%ebp)
        jnz     break_calldebug
        NEXT                    # debug hook not active, ignore

    break_calldebug:
        # store the status in the AMX (FRM, STK, HEA, CIP, and PRI + ALT)
        mov     %eax,_pri(%ebp)
        mov     %edx,_alt(%ebp) # EAX and EDX are now free to use
        mov     FRM,%eax
        mov     HEA,%edx
        mov     %eax,_frm(%ebp) # store values in AMX structure (STK, FRM & HEA)
        mov     %edx,_hea(%ebp)
        mov     %ecx,_stk(%ebp)
        mov     %esi,%eax
        sub     CODE,%eax       # EAX = CIP (relative to start of code segment)
        mov     %eax,_cip(%ebp)
        # call the debug hook
        mov     %ebp,%eax       # 1st parm: amx
        _SAVEREGS
        push    %eax
        call    *_debug(%ebp)   # call debug function
        _DROPARGS $4            # remove arguments from stack
        cmp     $AMX_ERR_NONE,%eax
        je      break_noabort   # continue running
        mov     %eax,_error(%ebp)   # save EAX (error code) before restoring all regs
        _RESTOREREGS            # abort run, but restore stack first
        mov     _error(%ebp),%eax   # get error code in EAX back again
        jmp     _return         # return error code
        break_noabort:
        _RESTOREREGS
        mov     _pri(%ebp),%eax # restore PRI and ALT
        mov     _alt(%ebp),%edx
        NEXT


OP_CASETBL:
OP_CASETBL_OVL:
        mov     $AMX_ERR_INVINSTR,%eax
        jmp     _return


OP_SYSREQ_D:
        mov     4(%esi),%ebx    # get function address
        mov     AMX,%ebp        # get amx into ebp
        add     $8,%esi

        mov     %ecx,STK        # save STK
        mov     %edx,ALT        # save ALT

        mov     %ecx,_stk(%ebp) # store values in AMX structure (STK, HEA, FRM)
        mov     HEA,%ecx
        mov     FRM,%eax
        mov     %ecx,_hea(%ebp)
        mov     %eax,_frm(%ebp) # eax & ecx are invalid by now

        mov     %ebp,%eax       # 1st param: amx
        mov     STK,%edx
        add     %edi,%edx       # 2nd param: addr. of function parameters
        # save a few registers (it is not necessary to save them all
        # and EAX should *not* be saved because it will hold the return
        # value)
        push    %ebp
        push    %esi
        push    %edi
        # push the parameters
        push    %edx
        push    %eax
        calll   *%ebx           # direct call
        _DROPARGS $8            # remove arguments from stack
        pop     %edi            # restore saved registers
        pop     %esi
        pop     %ebp
        cmpl    $AMX_ERR_NONE,_error(%ebp)
        jne     _return         # return error code, if any

        # function result is in eax (PRI)
        mov     ALT,%edx        # restore ALT
        mov     FRM,%ebx
        mov     STK,%ecx        # restore STK
        add     %edi,%ebx       # restore FRM
        NEXT


OP_SYSREQ_ND:
        mov     8(%esi),%ebp    # get # of bytes passed as parameters
        mov     4(%esi),%ebx    # get function number
        _PUSH   %ebp             # push 2nd parameter
        add     $12,%esi
        mov     AMX,%ebp        # get amx into ebp

        mov     %ecx,STK        # save STK
        mov     %edx,ALT        # save ALT

        mov     %ecx,_stk(%ebp) # store values in AMX structure (STK, HEA, FRM)
        mov     HEA,%ecx
        mov     FRM,%eax
        mov     %ecx,_hea(%ebp)
        mov     %eax,_frm(%ebp) # eax & ecx are invalid by now

        mov     %ebp,%eax       # 1st param: amx
        mov     STK,%edx
        add     %edi,%edx       # 2nd param: addr. of function parameters
        # save a few registers (it is not necessary to save them all
        # and EAX should *not* be saved because it will hold the return
        # value)
        push    %ebp
        push    %esi
        push    %edi
        # push the parameters
        push    %edx
        push    %eax
        calll   *%ebx           # direct call
        _DROPARGS $8            # remove arguments from stack
        pop     %edi            # restore saved registers
        pop     %esi
        pop     %ebp
        # function result is in eax (PRI)
        mov     ALT,%edx        # restore ALT
        mov     FRM,%ebx
        mov     STK,%ecx        # restore STK
        add     %edi,%ebx       # restore FRM
        add     -4(%esi),%ecx   # remove "number of parameter bytes" from the stack
        add     $4,%ecx         # also remove the extra DWORD pushed

        cmpl     $AMX_ERR_NONE,_error(%ebp)
        jne     _return         # return error code, if any
        NEXT


# overlay instructions
.ifndef AMX_NO_OVERLAY

OP_CALL_OVL:
        mov     %edx,ALT        # save ALT
        lea     8(%esi),%edx    # EDX=address of next instruction
        sub     CODE,%edx       # EDX=relative address (to start of code segment)
        mov     AMX,%ebp
        shl     $16,%edx
        or      _ovl_index(%ebp),%edx   # EDX=(relative address << 16) | ovl_index
        _PUSH   %edx
        mov     4(%esi),%edx    # EDX=ovl_index
        mov     %edx,_ovl_index(%ebp)
        mov     %ebp,%eax       # 1st parm: amx
        _SAVEREGS
        push    %edx            # EDX (2nd parm)=overlay index
        push    %eax
        call    *_overlay(%ebp) # call overlay function
        _DROPARGS $8            # remove arguments from stack
        _RESTOREREGS
        mov     ALT,%edx        # restore ALT
        mov     _codeseg(%ebp),%esi     # get new code base
        mov     %esi,CODE       # save new code base in local variable
        NEXT


OP_RETN_OVL:
        mov     %eax,PRI        # save PRI
        mov     %edx,ALT        # save ALT
        _POP    %ebx            # restore FRM
        _POP    %esi            # restore code offset + overlay index
        mov     %ebx,FRM
        add     %edi,%ebx
        mov     %esi,%edx
        mov     AMX,%ebp
        and     $0x0ffff,%edx   # EDX=overlay index (popped from stack)
        mov     %edx,_ovl_index(%ebp)   # store overlay index returning to
        shr     $16,%esi        # ESI=offset into overlay
        mov     (%edi,%ecx),%eax        # EAX=dataseg[stk]
        lea     4(%ecx,%eax),%ecx       # STK=STK+dataseg[stk]+4
        mov     %ebp,%eax       # 1st parm: amx
        _SAVEREGS
        push    %edx            # EDX (2nd parm)=overlay index
        push    %eax
        call    *_overlay(%ebp) # call overlay function
        _DROPARGS $8            # remove arguments from stack
        _RESTOREREGS
        mov     _codeseg(%ebp),%eax     # get new code base
        mov     %eax,CODE       # save new code base in local variable
        add     %eax,%esi       # ESI=code base + offset
        mov     PRI,%eax        # restore PRI
        mov     ALT,%edx        # restore ALT
        NEXT


OP_SWITCH_OVL:
        push    %ecx
        mov     %esi,%ebp       # EBP = CIP
        add     4(%esi),%ebp    # EBP = offset of the icasetable
        add     $4,%ebp         # skip the "OP_ICASETBL" opcode
        mov     (%ebp),%ecx     # ECX = number of records
        mov     4(%ebp),%edx    # preset EDX to "none-matched" case
    op_iswitch_loop:
        or      %ecx,%ecx       # number of records == 0?
        jz      op_iswitch_end  # yes, no more records, exit loop
        add     $8,%ebp         # skip previous record
        dec     %ecx            # already decrement cases to do
        cmp     (%ebp),%eax     # PRI == icase label?
        jne     op_iswitch_loop # no, continue loop
        mov     4(%ebp),%edx    # yes, get jump address and exit loop
    op_iswitch_end:
        pop     %ecx
        # load overlay
        mov     AMX,%eax
        mov     %edx,_ovl_index(%eax)
        _SAVEREGS
        push    %edx            # EDX (2nd parm)=overlay index
        push    %eax            # EAX (1st parm)=amx structure
        call    *_overlay(%eax) # call overlay function
        _DROPARGS $8            # remove arguments from stack
        _RESTOREREGS
        mov     _codeseg(%eax),%esi # get new code base
        mov     %esi,CODE       # save new code base in local variable
        NEXT

.else   # AMX_NO_OVERLAY

OP_CALL_OVL:
OP_RETN_OVL:
OP_SWITCH_OVL:
        mov     $AMX_ERR_INVINSTR,%eax
        jmp     _return

.endif  # AMX_NO_OVERLAY


# supplemental instructions
.ifndef AMX_NO_MACRO_INSTR

OP_LIDX:
        lea     (%edx,%eax,4),%eax
        add     $4,%esi
        _VERIFYADDRESS %eax
        mov     (%edi,%eax),%eax
        NEXT


OP_LIDX_B:
        push    %ecx
        mov     4(%esi),%ecx
        add     $8,%esi
        shl     %cl,%eax
        pop     %ecx
        add     %edx,%eax
        _VERIFYADDRESS %eax
        mov     (%edi,%eax),%eax
        NEXT


OP_IDXADDR:
        add     $4,%esi
        lea     (%edx,%eax,4),%eax
        NEXT


OP_IDXADDR_B:
        push    %ecx
        mov     4(%esi),%ecx
        add     $8,%esi
        shl     %cl,%eax
        pop     %ecx
        add     %edx,%eax
        NEXT


OP_PUSH_C:
        mov     4(%esi),%ebp
        add     $8,%esi
        _PUSH   %ebp
        NEXT


OP_PUSH:
        mov     4(%esi),%ebp
        add     $8,%esi
        mov     (%ebp,%edi),%ebp
        _PUSH   %ebp
        NEXT


OP_PUSH_S:
        mov     4(%esi),%ebp
        add     $8,%esi
        mov     (%ebp,%ebx),%ebp
        _PUSH   %ebp
        NEXT


OP_PUSH_ADR:
        mov     4(%esi),%ebp
        add     $8,%esi
        add     FRM,%ebp
        _PUSH   %ebp
        NEXT


OP_PUSHR_C:
        mov     4(%esi),%ebp
        add     $8,%esi
        add     %edi,%ebp
        _PUSH   %ebp
        NEXT


OP_PUSHR_S:
        mov     4(%esi),%ebp
        add     $8,%esi
        mov     (%ebp,%ebx),%ebp
        add     %edi,%ebp
        _PUSH   %ebp
        NEXT


OP_PUSHR_ADR:
        mov     4(%esi),%ebp
        add     $8,%esi
        add     %ebx,%ebp       # EBX = dat+frm
        _PUSH   %ebp
        NEXT


OP_JEQ:
        cmp     %edx,%eax
        je      OP_JUMP
        add     $8,%esi
        NEXT


OP_JNEQ:
        cmp     %edx,%eax
        jne     OP_JUMP
        add     $8,%esi
        NEXT


OP_JSLESS:
        cmp     %edx,%eax
        jl      OP_JUMP
        add     $8,%esi
        NEXT


OP_JSLEQ:
        cmp     %edx,%eax
        jle     OP_JUMP
        add     $8,%esi
        NEXT


OP_JSGRTR:
        cmp     %edx,%eax
        jg      OP_JUMP
        add     $8,%esi
        NEXT


OP_JSGEQ:
        cmp     %edx,%eax
        jge     OP_JUMP
        add     $8,%esi
        NEXT


OP_SUB_INV:
        add     $4,%esi
        sub     %edx,%eax
        NEXT


OP_ADD_C:
        add     4(%esi),%eax
        add     $8,%esi
        NEXT


OP_SMUL_C:
        mov     4(%esi),%ebp
        push    %edx
        imul    %ebp
        pop     %edx
        add     $8,%esi
        NEXT


OP_ZERO_PRI:
        add     $4,%esi
        sub     %eax,%eax
        NEXT


OP_ZERO_ALT:
        add     $4,%esi
        sub     %edx,%edx
        NEXT


OP_ZERO:
        mov     4(%esi),%ebp
        add     $8,%esi
        movl    $0,(%edi,%ebp)
        NEXT


OP_ZERO_S:
        mov     4(%esi),%ebp
        add     $8,%esi
        movl    $0,(%ebx,%ebp)
        NEXT


OP_EQ_C_PRI:
        cmp     4(%esi),%eax    # PRI == value ?
        lea     8(%esi),%esi
        mov     $0,%eax
        seteb   %al
        NEXT


OP_EQ_C_ALT:
        xor     %eax,%eax
        cmp     4(%esi),%edx    # ALT == value ?
        lea     8(%esi),%esi
        seteb   %al
        NEXT


OP_INC:
        mov     4(%esi),%ebp
        add     $8,%esi
        incl    (%edi,%ebp)
        NEXT


OP_INC_S:
        mov     4(%esi),%ebp
        add     $8,%esi
        incl    (%ebx,%ebp)
        NEXT


OP_DEC:
        mov     4(%esi),%ebp
        add     $8,%esi
        decl    (%edi,%ebp)
        NEXT


OP_DEC_S:
        mov     4(%esi),%ebp
        add     $8,%esi
        decl    (%ebx,%ebp)
        NEXT


OP_SYSREQ_N:
        mov     8(%esi),%ebp    # get # of bytes passed as parameters
        mov     4(%esi),%eax    # get function number
        _PUSH   %ebp            # push 2nd parameter
        add     $12,%esi
        mov     AMX,%ebp        # get amx into ebp

        mov     %ecx,STK        # save STK
        mov     %edx,ALT        # save ALT

        mov     %ecx,_stk(%ebp) # store values in AMX structure (STK, HEA, FRM)
        mov     HEA,%ecx
        mov     FRM,%ebx
        mov     %ecx,_hea(%ebp)
        mov     %ebx,_frm(%ebp) # ebx & ecx are invalid by now

        mov     %esi,%ebx       # also store CIP
        sub     CODE,%ebx
        mov     %ebx,_cip(%ebp)

        mov     %eax,%edx       # 2nd param: function number
        mov     %ebp,%eax       # 1st param: amx
        mov     STK,%ecx
        lea     PRI,%ebx        # 3rd param: addr. of retval
        add     %edi,%ecx       # 4th param: addr. of function parameters
        # save a few registers (it is not necessary to save them all
        # and EAX should *not* be saved because it will hold the return
        # value)
        push    %ebp
        push    %esi
        push    %edi
        # push the parameters
        push    %ecx
        push    %ebx
        push    %edx
        push    %eax
        call    *_callback(%ebp)
        _DROPARGS       $0x10   # remove arguments from stack
        pop     %edi            # restore saved registers
        pop     %esi
        pop     %ebp
        mov     ALT,%edx        # restore ALT
        mov     FRM,%ebx
        mov     STK,%ecx        # restore STK
        add     %edi,%ebx       # restore FRM
        add     -4(%esi),%ecx   # remove "number of parameter bytes" from the stack
        add     $4,%ecx         # also remove the extra DWORD pushed

        cmp     $AMX_ERR_NONE,%eax
        jne     _return         # return error code, if any
        mov     PRI,%eax        # get retval into eax (PRI)
        NEXT


OP_PUSHM_C:
        mov     %eax,PRI        # save PRI
        add     $8,%esi         # skip opcode and parameter count
        mov     -4(%esi),%eax   # get parameter count
    op_pushm_c_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        _PUSH   %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushm_c_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_PUSHM:
        mov     %eax,PRI        # save PRI
        add     $8,%esi         # skip opcode and parameter count
        mov     -4(%esi),%eax   # get parameter count
    op_pushm_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        mov     (%ebp,%edi),%ebp
        _PUSH   %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushm_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_PUSHM_S:
        mov     %eax,PRI        # save PRI
        add     $8,%esi         # skip opcode and parameter count
        mov     -4(%esi),%eax   # get parameter count
    op_pushm_s_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        mov     (%ebp,%ebx),%ebp
        _PUSH   %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushm_s_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_PUSHM_ADR:
        mov     %eax,PRI        # save PRI
        add     $8,%esi         # skip opcode and parameter count
        mov     -4(%esi),%eax   # get parameter count
    op_pushm_adr_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        add     FRM,%ebp
        _PUSH   %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushm_adr_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_PUSHRM_C:
        mov     %eax,PRI        # save PRI
        add     $8,%esi         # skip opcode and parameter count
        mov     -4(%esi),%eax   # get parameter count
    op_pushrm_c_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        add     %edi,%ebp
        _PUSH   %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushrm_c_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_PUSHRM_S:
        mov     %eax,PRI        # save PRI
        add     $8,%esi         # skip opcode and parameter count
        mov     -4(%esi),%eax   # get parameter count
    op_pushrm_s_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        mov     (%ebp,%ebx),%ebp
        add     %edi,%ebp
        _PUSH   %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushrm_s_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_PUSHRM_ADR:
        mov     %eax,PRI        # save PRI
        add     $8,%esi         # skip opcode and parameter count
        mov     -4(%esi),%eax   # get parameter count
    op_pushrm_adr_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        add     %ebx,%ebp       # EBX = dat+frm
        _PUSH   %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushrm_adr_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_LOAD2:
        mov     4(%esi),%eax
        mov     8(%esi),%edx
        add     $12,%esi
        mov     (%edi,%eax),%eax
        mov     (%edi,%edx),%edx
        NEXT


OP_LOAD2_S:
        mov     4(%esi),%eax
        mov     8(%esi),%edx
        add     $12,%esi
        mov     (%ebx,%eax),%eax
        mov     (%ebx,%edx),%edx
        NEXT


OP_CONST:
        push    %eax
        mov     4(%esi),%ebp
        mov     8(%esi),%eax
        add     $12,%esi
        mov     %eax,(%ebp,%edi)
        pop     %eax
        NEXT


OP_CONST_S:
        push    %eax
        mov     4(%esi),%ebp
        mov     8(%esi),%eax
        add     $12,%esi
        mov     %eax,(%ebp,%ebx)
        pop     %eax
        NEXT

.endif                  # AMX_NO_MACRO_INSTR


# packed opcodes
.ifndef AMX_NO_PACKED_OPC

OP_LOAD_P_PRI:
        GETPARAM_P %eax
        add     $4,%esi
        mov     (%edi,%eax),%eax
        NEXT

OP_LOAD_P_ALT:
        GETPARAM_P %edx
        add     $4,%esi
        mov     (%edi,%edx),%edx
        NEXT


OP_LOAD_P_S_PRI:
        GETPARAM_P %eax
        add     $4,%esi
        mov     (%ebx,%eax),%eax
        NEXT


OP_LOAD_P_S_ALT:
        GETPARAM_P %edx
        add     $4,%esi
        mov     (%ebx,%edx),%edx
        NEXT


OP_LREF_P_S_PRI:
        GETPARAM_P %eax
        add     $4,%esi
        mov     (%ebx,%eax),%eax
        mov     (%edi,%eax),%eax
        NEXT


OP_LREF_P_S_ALT:
        GETPARAM_P %edx
        add     $4,%esi
        mov     (%ebx,%edx),%edx
        mov     (%edi,%edx),%edx
        NEXT


OP_LODB_P_I:
        _VERIFYADDRESS %eax
        GETPARAM_P %ebp
        mov     (%edi,%eax),%eax        #subject to misalignment stalls
        add     $4,%esi
        and     (lodb_and-4)(,%ebp,4),%eax
        NEXT


OP_CONST_P_PRI:
        GETPARAM_P %eax
        add     $4,%esi
        NEXT


OP_CONST_P_ALT:
        GETPARAM_P %edx
        add     $4,%esi
        NEXT


OP_ADDR_P_PRI:
        GETPARAM_P %eax
        add     $4,%esi
        add     FRM,%eax
        NEXT


OP_ADDR_P_ALT:
        GETPARAM_P %edx
        add     $4,%esi
        add     FRM,%edx
        NEXT


OP_STOR_P:
        GETPARAM_P %ebp
        add     $4,%esi
        mov     %eax,(%ebp,%edi)
        NEXT


OP_STOR_P_S:
        GETPARAM_P %ebp
        add     $4,%esi
        mov     %eax,(%ebp,%ebx)
        NEXT


OP_SREF_P_S:
        GETPARAM_P %ebp
        add     $4,%esi
        mov     (%ebx,%ebp),%ebp
        mov     %eax,(%edi,%ebp)
        NEXT


OP_STRB_P_I:
        GETPARAM_P %ebp
        add     $4,%esi
        jmp     strb_entry


OP_LIDX_P_B:
        push    %ecx
        GETPARAM_P %ecx
        add     $4,%esi
        shl     %cl,%eax
        pop     %ecx
        add     %edx,%eax
        _VERIFYADDRESS %eax
        mov     (%edi,%eax),%eax
        NEXT


OP_IDXADDR_P_B:
        push    %ecx
        GETPARAM_P %ecx
        add     $4,%esi
        shl     %cl,%eax
        pop     %ecx
        add     %edx,%eax
        NEXT


OP_ALIGN_P_PRI:
        GETPARAM_P %ebp
        add     $4,%esi
        neg     %ebp            # ebp = -param
        add     $4,%ebp         # ebp = 4 - param
        xor     %ebp,%eax
        NEXT


OP_PUSH_P_C:
        GETPARAM_P %ebp
        add     $4,%esi
        _PUSH   %ebp
        NEXT


OP_PUSH_P:
        GETPARAM_P %ebp
        add     $4,%esi
        mov     (%ebp,%edi),%ebp
        _PUSH   %ebp
        NEXT


OP_PUSH_P_S:
        GETPARAM_P %ebp
        add     $4,%esi
        mov     (%ebp,%ebx),%ebp
        _PUSH   %ebp
        NEXT


OP_PUSH_P_ADR:
        GETPARAM_P %ebp
        add     $4,%esi
        add     FRM,%ebp
        _PUSH   %ebp
        NEXT


OP_PUSHR_P_C:
        GETPARAM_P %ebp
        add     $4,%esi
        add     %edi,%ebp
        _PUSH   %ebp
        NEXT


OP_PUSHR_P_S:
        GETPARAM_P %ebp
        add     $4,%esi
        mov     (%ebp,%ebx),%ebp
        add     %edi,%ebp
        _PUSH   %ebp
        NEXT


OP_PUSHR_P_ADR:
        GETPARAM_P %ebp
        add     $4,%esi
        add     %ebx,%ebp       # EBX = dat+frm
        _PUSH   %ebp
        NEXT


OP_PUSHM_P_C:
        mov     %eax,PRI        # save PRI
        GETPARAM_P %eax         # get parameter count
        add     $4,%esi
    op_pushm_p_c_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        _PUSH   %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushm_p_c_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_PUSHM_P:
        mov     %eax,PRI        # save PRI
        GETPARAM_P %eax         # get parameter count
        add     $4,%esi
    op_pushm_p_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        mov     (%ebp,%edi),%ebp
        _PUSH   %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushm_p_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_PUSHM_P_S:
        mov     %eax,PRI        # save PRI
        GETPARAM_P %eax         # get parameter count
        add     $4,%esi
    op_pushm_p_s_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        mov     (%ebp,%ebx),%ebp
        _PUSH   %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushm_p_s_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_PUSHM_P_ADR:
        mov     %eax,PRI        # save PRI
        GETPARAM_P %eax         # get parameter count
        add     $4,%esi
    op_pushm_p_adr_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        add     FRM,%ebp
        _PUSH   %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushm_p_adr_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_PUSHRM_P_C:
        mov     %eax,PRI        # save PRI
        GETPARAM_P %eax         # get parameter count
        add     $4,%esi
    op_pushrm_p_c_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        add     %edi,%ebp
        _PUSH   %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushrm_p_c_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_PUSHRM_P_S:
        mov     %eax,PRI        # save PRI
        GETPARAM_P %eax         # get parameter count
        add     $4,%esi
    op_pushrm_p_s_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        mov     (%ebp,%ebx),%ebp
        add     %edi,%ebp
        _PUSH   %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushrm_p_s_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_PUSHRM_P_ADR:
        mov     %eax,PRI        # save PRI
        GETPARAM_P %eax         # get parameter count
        add     $4,%esi
    op_pushrm_p_adr_loop:
        mov     (%esi),%ebp     # get and push parameter
        add     $4,%esi
        add     %ebx,%ebp       # EBX = dat+frm
        _PUSH  %ebp
        dec     %eax            # decrement cound and loop
        jnz     op_pushrm_p_adr_loop
        mov     PRI,%eax        # restore PRI
        NEXT


OP_STACK_P:
        GETPARAM_P %ebp
        add     $4,%esi
        add     %ebp,%ecx
        mov     %ecx,%edx
        _CHKMARGIN
        _CHKSTACK
        NEXT


OP_HEAP_P:
        GETPARAM_P %ebp
        add     $4,%esi
        mov     HEA,%edx
        add     %ebp,HEA
        _CHKMARGIN
        _CHKHEAP
        NEXT


OP_SHL_P_C_PRI:
        push    %ecx
        GETPARAM_P %ecx
        add     $4,%esi
        shl     %cl,%eax
        pop     %ecx
        NEXT


OP_SHL_P_C_ALT:
        push    %ecx
        GETPARAM_P %ecx
        add     $4,%esi
        shl     %cl,%edx
        pop     %ecx
        NEXT


OP_ADD_P_C:
        GETPARAM_P %ebp
        add     $4,%esi
        add     %ebp,%eax
        NEXT


OP_SMUL_P_C:
        GETPARAM_P %ebp
        add     $4,%esi
        push    %edx
        imul    %ebp
        pop     %edx
        NEXT


OP_ZERO_P:
        GETPARAM_P %ebp
        add     $4,%esi
        movl    $0,(%edi,%ebp)
        NEXT


OP_ZERO_P_S:
        GETPARAM_P %ebp
        add     $4,%esi
        movl    $0,(%ebx,%ebp)
        NEXT


OP_EQ_P_C_PRI:
        GETPARAM_P %ebp
        add     $4,%esi
        cmp     %ebp,%eax       # PRI == value ?
        mov     $0,%eax
        seteb   %al
        NEXT


OP_EQ_P_C_ALT:
        GETPARAM_P %ebp
        add     $4,%esi
        xor     %eax,%eax
        cmp     4(%esi),%edx    # ALT == value ?
        seteb   %al
        NEXT


OP_INC_P:
        GETPARAM_P %ebp
        add     $4,%esi
        incl    (%edi,%ebp)
        NEXT


OP_INC_P_S:
        GETPARAM_P %ebp
        add     $4,%esi
        incl    (%ebx,%ebp)
        NEXT


OP_DEC_P:
        GETPARAM_P %ebp
        add     $4,%esi
        decl    (%edi,%ebp)
        NEXT


OP_DEC_P_S:
        GETPARAM_P %ebp
        add     $4,%esi
        decl    (%ebx,%ebp)
        NEXT


OP_MOVS_P:
        _VERIFYADDRESS %eax     # PRI
        _VERIFYADDRESS %edx     # ALT
        GETPARAM_P %ebp
        add     %eax,%ebp
        dec     %ebp
        _VERIFYADDRESS %ebp     # PRI + size - 1
        sub     %eax,%ebp       # EBP = size - 1
        add     %edx,%ebp
        _VERIFYADDRESS %ebp     # ALT + size - 1

        push    %ecx            # matching POP is in movs_entry
        GETPARAM_P %ecx
        add     $4,%esi
        jmp     movs_entry


OP_CMPS_P:
        _VERIFYADDRESS %eax     # PRI
        _VERIFYADDRESS %edx     # ALT
        GETPARAM_P %ebp
        add     %eax,%ebp
        dec     %ebp            # EBP = PRI + size - 1
        _VERIFYADDRESS %ebp     # PRI + size - 1
        sub     %eax,%ebp       # EBP = size - 1
        add     %edx,%ebp       # EBP = ALT + size - 1
        _VERIFYADDRESS %ebp     # ALT + size - 1

        push    %ecx            # matching pop is in cmps_entry
        GETPARAM_P %ecx
        add     $4,%esi
        jmp     cmps_entry


OP_FILL_P:
        GETPARAM_P %ebp         # get byte count
        add     $4,%esi
        jmp     fill_entry


OP_HALT_P:
        cmpl    $0,RETVAL
        je      halt_no_retval_p
        mov     RETVAL,%ebp
        mov     %eax,(%ebp)
        halt_no_retval_p:
        # store the complete status in the AMX
        mov     AMX,%ebp        # get amx into ebp
        mov     %eax,_pri(%ebp) # store values in AMX structure (PRI, ALT, STK, HEA, FRM, ...)
        mov     %edx,_alt(%ebp)
        mov     %ecx,_stk(%ebp)
        mov     HEA,%ecx
        mov     FRM,%ebx
        mov     %ecx,_hea(%ebp)
        mov     %ebx,_frm(%ebp) # EBX & ECX are invalid by now
        GETPARAM_P %ebx         # EBX=parameter of the HALT opcode
        add     $4,%esi         # skip this instruction
        mov     %esi,%eax       # EAX=CIP
        sub     CODE,%eax
        mov     %eax,_cip(%ebp)
        mov     %ebx,%eax       # return the parameter of the HALT opcode
        jmp     _return


OP_BOUNDS_P:
        GETPARAM_P %ebp
        add     $4,%esi
        cmp     %ebp,%eax
        ja      err_bounds      # use unsigned comparison, so <0 is >bounds
        NEXT

.endif                  # AMX_NO_PACKED_OPC


err_call:
        mov     $AMX_ERR_CALLBACK,%eax
        jmp     _return

err_stack:
        mov     $AMX_ERR_STACKERR,%eax
        jmp     _return

err_stacklow:
        mov     $AMX_ERR_STACKLOW,%eax
        jmp     _return

err_memaccess:
        mov     $AMX_ERR_MEMACCESS,%eax
        jmp     _return

err_bounds:
        mov     $AMX_ERR_BOUNDS,%eax
        jmp     _return

err_heaplow:
        mov     $AMX_ERR_HEAPLOW,%eax
        jmp     _return

err_divide:
        mov     $AMX_ERR_DIVIDE,%eax
        jmp     _return


_return:
        # save a few parameters, mostly for the "sleep" function
        mov     AMX,%ebp        # get amx into ebp
        mov     %eax,_pri(%ebp) # store values in AMX structure (PRI, ALT)
        mov     %edx,_alt(%ebp) # store values in AMX structure (PRI, ALT)

        pop     %esi    # remove FRM from stack

        pop     %ecx
        pop     %ebx
        pop     %edx

        pop     %esi    # remove pointer to amx from stack
        pop     %esi    # remove code segment pointer
        pop     %esi    # remove code size

        add     $4*3,%esp       # place for PRI, ALT & STK at SYSREQs

        pop     %ebp
        pop     %esi
        pop     %edi
        pop     %ebx
        ret


#----------------------------------------------------------------------------
#int amx_exec_list(const AMX *amx,const cell **opcodelist,int *numopcodes)
#                             eax              edx             ebx
#----------------------------------------------------------------------------

        .global _amx_exec_list
        .global _amx_exec_list@12
_amx_exec_list@12:
_amx_exec_list:
        push    %ebx
        #mov     eax,[esp+08h]  # amx parameter is not used
        mov     0x0c(%esp),%edx # opcodelist parameter
        mov     0x10(%esp),%ebx # numopcodes parameter

        mov     $opcodelist,%eax
        mov     %eax,(%edx)
        neg     %eax
        add     $opcodelist_end,%eax    # EAX = terminator - start
        shr     $2,%eax
        mov     %eax,(%ebx)     # store number of opcodes
        xor     %eax,%eax       # return value = 0

        pop     %ebx
        ret


#============================================================================

# section for constant data is called .rdata in Windows and .rodata in Linux
#ifdef __MINGW32__
    .section .rdata
#else
    .section .rodata
#endif
        .align  4       # This is essential to avoid misalignment stalls.

lodb_and:       .long   0x0ffffffff,0,0x0ffff,0x0ff

opcodelist:
# core set
        .long   OP_NOP
        .long   OP_LOAD_PRI
        .long   OP_LOAD_ALT
        .long   OP_LOAD_S_PRI
        .long   OP_LOAD_S_ALT
        .long   OP_LREF_S_PRI
        .long   OP_LREF_S_ALT
        .long   OP_LOAD_I
        .long   OP_LODB_I
        .long   OP_CONST_PRI
        .long   OP_CONST_ALT
        .long   OP_ADDR_PRI
        .long   OP_ADDR_ALT
        .long   OP_STOR
        .long   OP_STOR_S
        .long   OP_SREF_S
        .long   OP_STOR_I
        .long   OP_STRB_I
        .long   OP_ALIGN_PRI
        .long   OP_LCTRL
        .long   OP_SCTRL
        .long   OP_XCHG
        .long   OP_PUSH_PRI
        .long   OP_PUSH_ALT
        .long   OP_PUSHR_PRI
        .long   OP_POP_PRI
        .long   OP_POP_ALT
        .long   OP_PICK
        .long   OP_STACK
        .long   OP_HEAP
        .long   OP_PROC
        .long   OP_RET
        .long   OP_RETN
        .long   OP_CALL
        .long   OP_JUMP
        .long   OP_JZER
        .long   OP_JNZ
        .long   OP_SHL
        .long   OP_SHR
        .long   OP_SSHR
        .long   OP_SHL_C_PRI
        .long   OP_SHL_C_ALT
        .long   OP_SMUL
        .long   OP_SDIV
        .long   OP_ADD
        .long   OP_SUB
        .long   OP_AND
        .long   OP_OR
        .long   OP_XOR
        .long   OP_NOT
        .long   OP_NEG
        .long   OP_INVERT
        .long   OP_EQ
        .long   OP_NEQ
        .long   OP_SLESS
        .long   OP_SLEQ
        .long   OP_SGRTR
        .long   OP_SGEQ
        .long   OP_INC_PRI
        .long   OP_INC_ALT
        .long   OP_INC_I
        .long   OP_DEC_PRI
        .long   OP_DEC_ALT
        .long   OP_DEC_I
        .long   OP_MOVS
        .long   OP_CMPS
        .long   OP_FILL
        .long   OP_HALT
        .long   OP_BOUNDS
        .long   OP_SYSREQ
        .long   OP_SWITCH
        .long   OP_SWAP_PRI
        .long   OP_SWAP_ALT
        .long   OP_BREAK
        .long   OP_CASETBL
# patched instructions
        .long   OP_SYSREQ_D
        .long   OP_SYSREQ_ND
# overlay instructions
        .long   OP_CALL_OVL
        .long   OP_RETN_OVL
        .long   OP_SWITCH_OVL
        .long   OP_CASETBL_OVL
# supplemental instructions
.ifndef AMX_NO_MACRO_INSTR
        .long   OP_LIDX
        .long   OP_LIDX_B
        .long   OP_IDXADDR
        .long   OP_IDXADDR_B
        .long   OP_PUSH_C
        .long   OP_PUSH
        .long   OP_PUSH_S
        .long   OP_PUSH_ADR
        .long   OP_PUSHR_C
        .long   OP_PUSHR_S
        .long   OP_PUSHR_ADR
        .long   OP_JEQ
        .long   OP_JNEQ
        .long   OP_JSLESS
        .long   OP_JSLEQ
        .long   OP_JSGRTR
        .long   OP_JSGEQ
        .long   OP_SDIV_INV
        .long   OP_SUB_INV
        .long   OP_ADD_C
        .long   OP_SMUL_C
        .long   OP_ZERO_PRI
        .long   OP_ZERO_ALT
        .long   OP_ZERO
        .long   OP_ZERO_S
        .long   OP_EQ_C_PRI
        .long   OP_EQ_C_ALT
        .long   OP_INC
        .long   OP_INC_S
        .long   OP_DEC
        .long   OP_DEC_S
        .long   OP_SYSREQ_N
        .long   OP_PUSHM_C
        .long   OP_PUSHM
        .long   OP_PUSHM_S
        .long   OP_PUSHM_ADR
        .long   OP_PUSHRM_C
        .long   OP_PUSHRM_S
        .long   OP_PUSHRM_ADR
        .long   OP_LOAD2
        .long   OP_LOAD2_S
        .long   OP_CONST
        .long   OP_CONST_S
.endif  # AMX_NO_MACRO_INSTR
# packed opcodes
.ifndef AMX_NO_PACKED_OPC
        .long   OP_LOAD_P_PRI
        .long   OP_LOAD_P_ALT
        .long   OP_LOAD_P_S_PRI
        .long   OP_LOAD_P_S_ALT
        .long   OP_LREF_P_S_PRI
        .long   OP_LREF_P_S_ALT
        .long   OP_LODB_P_I
        .long   OP_CONST_P_PRI
        .long   OP_CONST_P_ALT
        .long   OP_ADDR_P_PRI
        .long   OP_ADDR_P_ALT
        .long   OP_STOR_P
        .long   OP_STOR_P_S
        .long   OP_SREF_P_S
        .long   OP_STRB_P_I
        .long   OP_LIDX_P_B
        .long   OP_IDXADDR_P_B
        .long   OP_ALIGN_P_PRI
        .long   OP_PUSH_P_C
        .long   OP_PUSH_P
        .long   OP_PUSH_P_S
        .long   OP_PUSH_P_ADR
        .long   OP_PUSHR_P_C
        .long   OP_PUSHR_P_S
        .long   OP_PUSHR_P_ADR
        .long   OP_PUSHM_P_C
        .long   OP_PUSHM_P
        .long   OP_PUSHM_P_S
        .long   OP_PUSHM_P_ADR
        .long   OP_PUSHRM_P_C
        .long   OP_PUSHRM_P_S
        .long   OP_PUSHRM_P_ADR
        .long   OP_STACK_P
        .long   OP_HEAP_P
        .long   OP_SHL_P_C_PRI
        .long   OP_SHL_P_C_ALT
        .long   OP_ADD_P_C
        .long   OP_SMUL_P_C
        .long   OP_ZERO_P
        .long   OP_ZERO_P_S
        .long   OP_EQ_P_C_PRI
        .long   OP_EQ_P_C_ALT
        .long   OP_INC_P
        .long   OP_INC_P_S
        .long   OP_DEC_P
        .long   OP_DEC_P_S
        .long   OP_MOVS_P
        .long   OP_CMPS_P
        .long   OP_FILL_P
        .long   OP_HALT_P
        .long   OP_BOUNDS_P
.endif  # AMX_NO_PACKED_OPC

opcodelist_end:


