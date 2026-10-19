/* Regression test suite for the Pawn compiler and abstract machine */

/* Borland C++ (Windows)
 * ---------------------
 *   Create the DLLs with:
 *     bcc32 -w -w-prc -tWD -DAMXEXPORT="__stdcall _export" -DAMX_NATIVE_CALL=__stdcall amxFixed.c amx.c
 *     bcc32 -w -w-prc -tWD -DAMXEXPORT="__stdcall _export" -DAMX_NATIVE_CALL=__stdcall amxFloat.c amx.c
 *     bcc32 -w -w-prc -w-amb -tWD -DAMXEXPORT="__stdcall _export" -DAMX_NATIVE_CALL=__stdcall amxFile.c amx.c
 *     bcc32 -w -w-prc -w-amb -tWD -DAMXEXPORT="__stdcall _export" -DAMX_NATIVE_CALL=__stdcall -DAMX_NOSTRFMT amxString.c amx.c
 *   Create PAWNRUN with:
 *     bcc32 -w -w-prc -DFLOATPOINT;FIXEDPOINT -DPAWN_DLL -DAMXDBG;AMX_DYNALOAD pawnrun.c amx.c amxcore.c amxcons.c amxdbg.c
 *   Move all DLLs and executables to the "bin" directory.
 *
 * MinGW (Windows)
 * ---------------
 *   Create the DLLs with:
 *     gcc -Wall -Wextra -Wno-parentheses -DPAWN_DLL -DAMXEXPORT=__stdcall -DPAWN_CELL_SIZE=32 -o ..\bin\amxFixed.dll amxFixed.c amx.c dllmain.c amxFixed.def -shared -s -Wl,--subsystem,windows,--enable-stdcall-fixup
 *     gcc -Wall -Wextra -Wno-parentheses -DPAWN_DLL -DAMXEXPORT=__stdcall -DPAWN_CELL_SIZE=32 -o ..\bin\amxFloat.dll amxFloat.c amx.c dllmain.c amxFloat.def -shared -s -Wl,--subsystem,windows,--enable-stdcall-fixup
 *     gcc -Wall -Wextra -Wno-parentheses -DPAWN_DLL -DAMXEXPORT=__stdcall -DPAWN_CELL_SIZE=32 -o ..\bin\amxFile.dll amxFile.c amx.c dllmain.c amxFile.def -shared -s -Wl,--subsystem,windows,--enable-stdcall-fixup
 *     gcc -Wall -Wextra -Wno-parentheses -DPAWN_DLL -DAMXEXPORT=__stdcall -DPAWN_CELL_SIZE=32 -DAMX_NOSTRFMT -o ..\bin\amxString.dll amxString.c amx.c dllmain.c amxString.def -shared -s -Wl,--subsystem,windows,--enable-stdcall-fixup
 *   Create PAWNRUN with:
 *     gcc -Wall -Wextra -Wno-parentheses -DPAWN_DLL -DAMXEXPORT=__stdcall -DAMXDBG -DAMX_DYNALOAD -DFLOATPOINT -DFIXEDPOINT -DPAWN_CELL_SIZE=32 -o../bin/pawnrun pawnrun.c amx.c amxcore.c amxcons.c amxdbg.c
 *
 * GCC on Linux
 * ------------
 *   Create the shared objects with:
 *     gcc -Wall -Wextra -Wno-parentheses -DPAWN_CELL_SIZE=32 -I../linux -fpic -shared -o../bin/amxFixed.so amxfixed.c amx.c
 *     gcc -Wall -Wextra -Wno-parentheses -DPAWN_CELL_SIZE=32 -I../linux -fpic -shared -o../bin/amxFloat.so amxfloat.c amx.c -lm
 *     gcc -Wall -Wextra -Wno-parentheses -DPAWN_CELL_SIZE=32 -I../linux -fpic -shared -o../bin/amxFile.so amxfile.c amx.c
 *     gcc -Wall -Wextra -Wno-parentheses -DPAWN_CELL_SIZE=32 -DAMX_NOSTRFMT -I../linux -fpic -shared -o../bin/amxString.so amxstring.c amx.c
 *   Create PAWNRUN with:
 *     gcc -Wall -Wextra -Wno-parentheses -DENABLE_BINRELOC -DAMXDBG -DAMX_DYNALOAD -DFLOATPOINT -DFIXEDPOINT -DPAWN_CELL_SIZE=32 -I../linux -o../bin/pawnrun pawnrun.c amx.c amxcore.c amxcons.c amxdbg.c ../linux/getch.c ../linux/binreloc.c
 */

say 'This REXX file does several tests on the compiler and the abstract machine.'
say 'The test scrip assumes that both the compiler and the abstract machine can be'
say 'found relative to the parent directory of this directory.'
say ''
say 'The current system is' uname('S')'.'
say ''
say 'Requirements:'
say '   1. PAWNCC, PAWNRUN and PAWNDISASM are compiled with assertions'
say '   2. PAWNCC and PAWNDISASM use the FORTIFY library for memory leakage checks'
say '   3. PAWNRUN is the ANSI C version (for most assertions) and includes AMXDBG'
say '   4. AMXCONS includes both floating-point and fixed-point support'
say '   5. DLLs/shared libraries (a.o. "amxFixed", "amxFloat") are present, and'
say '      PAWNRUN is built to include support for dynamically loaded modules'
say 'For example builds, see the comments in this REXX file.'
say ''
say 'At a ''test#'' prompt:'
say '   o  a number (followed by Enter) runs that specific test'
say '   o  an empty field (just press Enter) runs the next test'
say '   o  ''run'' goes through the tests automatically (until one fails)'
say '   o  ''bye'' aborts the test script'

/* check whether we are running in Windows or Linux */
sysname = uname('S')
if sysname = WINNT | sysname = WIN2K | sysname = WINXP | sysname = WIN7 | sysname = WIN8 | sysname = WIN10 | sysname = WIN11 | sysname = WIN95 | sysname = WIN98 then
  iswin32 = 1
else
  iswin32 = 0

if iswin32 then
  do
    clearscreen = 'cls'
    remove      = 'del'
    to_dev_null = '> nul 2>&1'
    pawncc      = '..\bin\pawncc'
    pawnrun     = '..\bin\pawnrun'
    pawndisasm  = '..\bin\pawndisasm'
  end
else
  do
    clearscreen = 'clear'
    remove      = 'rm'
    to_dev_null = '> /dev/null 2>&1'
    pawncc      = '../bin/pawncc'
    pawnrun     = '../bin/pawnrun'
    pawndisasm  = '../bin/pawndisasm'
  end

signal on syntax name syntax_err
trace O /* disable checking of the return code of the compiler */
testnumber = 0
autorun = 0
do forever
  if autorun = 1 then
    reply = ''
  else
    do
      say ''
      call charout , 'test# '
      pull reply
      if translate(reply) = 'BYE' | translate(reply) = 'QUIT' then
        call clean_files_and_exit
      if translate(reply) = 'RUN' then
        do
          autorun = 1
          reply = ''
        end
    end /* if autorun */
  ok = 1
  if reply = '' then
    testnumber = testnumber + 1
  else
    do
      if datatype(reply) = 'NUM' then
        testnumber = reply
      else
        do
          ok = 0
          say 'Invalid input, use a test number, or ''run'' or ''bye''.'
        end
    end

  if ok = 1 then
    do
     clearscreen
     testlabel = 'test'||testnumber
     logfile = testlabel||'.log'
     commandsuffix = '>>' logfile '2>&1'
     if stream(logfile) = 'READY' then
       remove logfile
     interpret 'signal' testlabel    /* interpret 'call' testlabel */
    end
end

/* matchstring needle, haystack
   - comparison is case-insensitive
   - 'needle' may contain '*' wildcards.
 */
matchstring:
  needle = translate(arg(1))
  haystack = translate(arg(2))
  p1 = 0
  do while needle <> ''
    /* split off left part of 'needle' pattern, store the remainder back in 'needle' */
    parse value needle with part '*' needle
    p2 = pos(part, haystack)
    if p2 = 0 | p2 <= p1 then
      return 0
    p1 = p2
  end
  return 1

/* matchoutput pattern1 [, pattern 2 [, ...]]
   - The 'pass' patterns must be all be matched, and must appear in the sequence
     of the argument list. There may be other strings (in the output file) in
     between two pass patterns, but they may not appear in inverted order.
   - None of the 'fail' patterns may match, and the order of the fail patterns
     is not important. If a fail pattern matches anywhere in the output file,
     the result is always 'fail'.
 */
matchoutput:
  /* check for 'fail' patterns (and split these off from the 'pass' patterns) */
  failexpr.0 = 0
  numpass = arg()
  do while left(arg(numpass), 1) = '/'
    n = failexpr.0 + 1
    failexpr.0 = n
    failexpr.n = translate(substr(arg(numpass), 2))
    numpass = numpass - 1
  end
  /* test expected output */
  forcefail = 0
  argidx = 1
  linein(logfile, 1, 0)             /* reset to first line */
  do while lines(logfile) > 0
    outputline = linein(logfile)
    say outputline
    outputline = translate(outputline)
    if argidx <= numpass & matchstring(arg(argidx), outputline) = 1 then
      argidx = argidx + 1
    do n = 1 to failexpr.0
      if matchstring(failexpr.n, outputline) = 1 then
        forcefail = 1
    end
  end
  say '------'
  esc = d2c(27)
  if argidx = numpass + 1 & forcefail = 0 then
    say esc || '[32m' 'Pass'
  else
    do
      say esc || '[91m' 'Fail'
      autorun = 0   /* stop running on failure */
    end
  say esc || '[0m'  /* reset colour */
  return

syntax_err:
  say 'All tests have completed.'
clean_files_and_exit:
  remove '*.log' to_dev_null
  remove '*.amx' to_dev_null
  remove '*.xml' to_dev_null
  exit

test1:
  say '1. The compilation should issue error 017 ("undefined symbol").'
  say ''
  say '   Calling an undefined function.'
  say '-----'
  pawncc '-v0 -p UNDEF_FUNC_CALL= test1' commandsuffix
  call matchoutput 'error 017', '1 Error.'
  /* Symptoms of detected bug: the compiler flags the error, but then crashes
     or hangs. */
  return

test2:
  say '2. The following test should compile successfully.'
  say ''
  say '   Compiling a program that contains no native function call should compile'
  say '   correctly.'
  say '-----'
  pawncc '-p test1' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: the compiler aborts with an "out of memory" error.
   */
  return

test3:
  say '3. The following test should compile successfully, showing verbose output.'
  say '   When running, a run-time assertion should fire (run time error 2), but'
  say '   NOT on line 11.'
  say ''
  say '   An assertion with a function call should give the correct line number.'
  say '-----'
  pawncc '-d2 ASSERT_LINENO= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'Run time error 2', 'test2.p*line:', '/line: 11'
  /* Symptoms of detected bug: the abstract machine returns line 11, because
     that is the last line executed (through the function call).
   */
  return

test4:
  say '4. The compilation should issue warning 229.'
  say ''
  say '   Array assignment: the following three tests attempt to assign arrays where'
  say '   the size does not match, or the size of an array is unspecified.'
  say '-----'
  pawncc '-v0 MIX_PACKED_UNPACKED= test3' commandsuffix
  call matchoutput 'warning 229', '1 Warning.'
  return

test5:
  say '5. The compilation should issue error 037 ("invalid string").'
  say ''
  say '   An unterminated string in a function that accepts a variable number of'
  say '   parameters re-enters the lexer indefinitely searching for the closing ")".'
  say '-----'
  pawncc '-v0 UNTERMINATED_STRING= test2' commandsuffix
  call matchoutput 'error 037', 'error 001', '2 Errors.', '/Assertion failed'
  /* Symptoms of detected bug: the compiler produces garbage on the screen or
     crashes.
   */
  return

test6:
  say '6. The following test issue warning 203.'
  say ''
  say '   Array assignment (testing whether valid syntaxes pass).'
  say '-----'
  pawncc 'OKAY= test3' commandsuffix
  call matchoutput 'warning 203', '1 Warning.'
  return

test7:
  say '7. The following THREE compilations should all issue error 047.'
  say ''
  say '   Array assignment: the following three tests attempt to assign arrays where'
  say '   the size does not match, or the size of an array is unspecified.'
  say '-----'
  pawncc '-v0 WRONG_LENGTH_1= test3' commandsuffix
  pawncc '-v0 WRONG_LENGTH_2= test3' commandsuffix
  pawncc '-v0 WRONG_LENGTH_3= test3' commandsuffix
  call matchoutput 'error 047', '1 Error.', 'error 047', '1 Error.', 'error 047', '1 Error.'
  return

test8:
  say '8. The compilation should issue error 006 (followed by warnings).'
  say ''
  say '   Array assignment: the program attempts to assign an array to an indexed cell'
  say '   in a second array. Both arrays have a single dimension. A cell cannot hold'
  say '   an array.'
  say '-----'
  pawncc '-v0 INDEXED= test3' commandsuffix
  call matchoutput 'error 006', '1 Error.'
  return

test9:
  say '9. The following test should compile successfully.'
  say ''
  say '    Line continuation (with a \ at the end of a line) could cause incorrect'
  say '    lines to be read.'
  say '-----'
  pawncc 'LINE_CAT= test2' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: compiler error "symbol not found" with a symbol
     name "prinprint".
   */
  return

test10:
  say '10. The compilation should issue warning 217.'
  say ''
  say '    The infamous "dangling else" problem should be signalled to the user with'
  say '    a "loose indentation" warning.'
  say '-----'
  pawncc '-v0 -p DANGLING_ELSE= test1' commandsuffix
  call matchoutput 'warning 217', '1 Warning.'
  /* Symptoms of detected bug: no warning message.
   */
  return

test11:
  say '11. The compilation should issue warning 203.'
  say ''
  say '    An unused and uninitialized local variable should be flagged as "never'
  say '    used" (warning 203).'
  say '-----'
  pawncc '-v0 -p UNUSED_LOCAL= test1' commandsuffix
  call matchoutput 'warning 203', '1 Warning.'
  /* Symptoms of detected bug: warning message "assigned a value that is never
     used" (warning 204).
   */
  return

test12:
  say '12. The compilation should issue warning 219 for TWO variables.'
  say ''
  say '    A local variable with the name of a variable or function (or parameter) at'
  say '    a higher level should issue a warning (because it is not "clean" code and'
  say '    possibly an error).'
  say '-----'
  pawncc '-v0 -p LOCAL_SHADOWS= test1' commandsuffix
  call matchoutput 'warning 219', 'warning 219', '2 Warnings.'
  /* Symptoms of detected bug: no warning message.
   */
  return

test13:
  say '13. The following test should compile successfully.'
  say ''
  say '    Function names with upper case letters are valid.'
  say '-----'
  pawncc '-p MIXED_CASE= test1' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: assertion failed (in the assembler).
   */
  return

test14:
  say '14. The following test should compile successfully.'
  say ''
  say '    The "for" loop would remove all local symbols of the higher level if the'
  say '    index was not declared in expr1 of the "for" loop.'
  say '-----'
  pawncc '-p FOR_DEL_LOCALS= test1' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: compiler error for one or more unknown variable(s)
     and an assertion failed (in the staging submodule).
   */
  return

test15:
  say '15. The following test should compile successfully (and show verbose output).'
  say ''
  say '    For systems that support long filenames (and filenames with embedded space'
  say '    characters), the code generator cannot use the space character as a name'
  say '    delimiter, when adding debugging information.'
  say '-----'
  pawncc '-d2 "test lfn"' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: assertion failure in the assembler stage.
   */
  return

test16:
  say '16. The following test should compile successfully.'
  say ''
  say '    A global variable that is a fairly large, uninitialized, array. This array'
  say '    should be initialized completely with zeros.'
  say '-----'
  pawncc '-p UNINIT_ARRAY= test1' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: when there are more than 16 array entries to set
     to zero, this causes an assertion failure in the assembler stage.
   */
  return

test17:
  say '17. The following test should compile successfully.'
  say ''
  say '    A prototyped function followed by its (identical) definition should'
  say '    compile.'
  say '-----'
  pawncc '-p PROTOTYPE_GOOD= test1' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: due to a string comparison error, the definition was
     considered *different* from its prototype if the parameter name is the same as
     the one on the prototype.
   */
  return

test18:
  say '18. The compilation should issue error 025.'
  say ''
  say '    Parameter names in a prototype must be identical to those in the function'
  say '    definition, otherwise the "named parameters" feature cannot work.'
  say '-----'
  pawncc '-v0 -p PROTOTYPE_BAD= test1' commandsuffix
  call matchoutput 'error 025', '1 Error.', '/Assertion failed'
  /* Symptoms of detected bug: the compiler accepted different parameter names (it
     rejected identical names; this bug is related to the previous one).
   */
  return

test19:
  say '19. The compilation should issue error 022.'
  say ''
  say '    An attempt to assign a value to a constant in a test should issue error 22.'
  say '-----'
  pawncc '-v0 -p LVAL_IN_TEST= test1' commandsuffix
  call matchoutput 'error 022', '1 Error.', '/Assertion failed'
  /* Symptoms of detected bug: the compiler detects the error but then fails in an
     assertion and/or crashes.
   */
  return

test20:
  say '20. The following test should compile successfully.'
  say ''
  say '    The example program contains valid declarations of public functions and'
  say '    public variables.'
  say '-----'
  pawncc 'test4' commandsuffix
  call matchoutput 'Completed successfully'
  return

test21:
  say '21. The compilation should issue error 042.'
  say ''
  say '    A variable is declared both "public" and "native" (which is invalid).'
  say '-----'
  pawncc '-v0 INVALID_PUBVAR= test4' commandsuffix
  call matchoutput 'error 042', 'error 010', '2 Errors.'
  return

test22:
  say '22. The next TWO compilations should each issue error 042.'
  say ''
  say '    Two functions are flagged as both "public" and "native" (which is invalid).'
  say '-----'
  pawncc '-v0 INVALID_PUBFUNC1= test4' commandsuffix
  pawncc '-v0 INVALID_PUBFUNC2= test4' commandsuffix
  call matchoutput 'error 042', 'error 010', '2 Errors.', 'error 042', '1 Error.'
  return

test23:
  say '23. The compilation should issue error 056 TWICE.'
  say ''
  say '    Both function arguments and local variables cannot be "public".'
  say '-----'
  pawncc '-v0 INVALID_PUBLOCAL= test4' commandsuffix
  call matchoutput 'error 056', 'error 056', '2 Errors.'
  return

test24:
  say '24. The compilation should issue warning 202 (too few parameters).'
  say ''
  say '    A function that expects two parameters of type array receives only one.'
  say '    Default parameter values are absent.'
  say '-----'
  pawncc '-v0 -p MISSING_PARM= test1' commandsuffix
  call matchoutput 'warning 202', '1 Warning.', '/Assertion failed'
  /* Symptoms of detected bug: the compiler issues the warning, but then crashes
     because it attempts to store the default value (which is not present) for the
     array.
   */
  return

test25:
  say '25. The following test should compile successfully.'
  say ''
  say '    When semicolons to end a line are optional, a postfix operator may not'
  say '    start a line; the operator should be considered the prefix operator on the'
  say '    next line.'
  say '-----'
  pawncc 'IMPLICIT_POSTFIX= test2' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: the prefix operator on the next line was considered
     a postfix operator related to the previous line.
   */
  return

test26:
  say '26. The following test should compile successfully. The output of the program'
  say '    must be 3 lines with the number 40 and a 4th line with the number 41.'
  say ''
  say '    A reference argument to a function that is passed to another function by'
  say '    reference is actually passed by value.'
  say '-----'
  pawncc 'REFERENCE_ARG= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'before: 40', 'func1: 40', 'func2: 40', 'after: 41'
  /* Symptoms of detected bug: the value of the argument was not loaded in the
     primary register before pushing it onto the stack.
   */
  return

test27:
  say '27. The following test should compile successfully.'
  say ''
  say '    A string or a literal array that is passed to a function must be smaller'
  say '    or equal to the size of the array of the function''s formal argument.'
  say '-----'
  pawncc 'PASS_LIT_ARRAY= test3' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: the compiler rejected all literal arrays (unless
     the function''s formal argument had an unspecified array size).
   */
  return

test28:
  say '28. The compilation should issue error 047 THREE times.'
  say ''
  say '    A string or a literal array that is passed to a function must be smaller'
  say '    or equal to the size of the array of the function''s formal argument.'
  say '-----'
  pawncc '-v0 PASS_WRONG_LENGTH= test3' commandsuffix
  call matchoutput 'error 047', 'error 047', 'error 047', '3 Errors.'
  return

test29:
  say '29. The following test should compile successfully (with only warning 203). The'
  say '    output of the program must read "*** for 63".'
  say ''
  say '    This module has a label (unused) in a somewhat larger program.'
  say '-----'
  pawncc 'test5' commandsuffix
  pawnrun 'test5.amx' commandsuffix
  call matchoutput 'warning 203', '1 Warning.', 'for 63'
  /* Symptoms of detected bug: wrong printout (or even a crash) due to the size of
     the "label" code not being accounted for in the code generation.
   */
  return

test30:
  say '30. The compilation should issue error 001, expecting "#endif" before end of'
  say '    file.'
  say ''
  say '    The program has an #if 0 without a matching #endif before the end of'
  say '    compilation.'
  say '-----'
  pawncc '-v0 test6' commandsuffix
  call matchoutput 'error 001*expected ''#endif''*<end of file>'
  /* Symptoms of detected bug: silent complation (no error/warning message).
   */
  return

test31:
  say '31. The compilation should issue error 001, expecting "*/" before end of file.'
  say ''
  say '    The program has a /* without a matching */ before the end of compilation.'
  say '-----'
  pawncc '-v0 test7' commandsuffix
  call matchoutput 'error 001*expected ''*/''*<end of file>'
  /* Symptoms of detected bug: silent complation (no error/warning message).
   */
  return

test32:
  say '32. The following test should compile successfully.'
  say ''
  say '    Conditional #endinput should reset the #if ... #endif nesting level.'
  say '-----'
  pawncc '-i. test8' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: #if ... section was left open, causing an error
     message at file close.
   */
  return

test33:
  say '33. The following test should compile successfully.'
  say ''
  say '    A recursive function that returns a value from a call to itself implicitly'
  say '    returns a value.'
  say '-----'
  pawncc 'fibr' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: warning 209 (function should return a value),
     because at the time the function is used, its definition is not yet complete.
   */
  return

test34:
  say '34. The following test should issue warning 206. When it runs, it should print'
  say '    the string "Hello world".'
  say ''
  say '    Comparing two constants with the same value is redundant, but should'
  say '    generate valid code.'
  say '-----'
  pawncc '-v0 REDUNDANT_TEST= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'warning 206', '1 Warning.', 'Hello world'/*, '/Assertion failed'*/
  /* Symptoms of detected bug: invalid opcode assertion when running it. The
     redundant test was scrapped and this caused code following it to be scrapped
     as well.
   */
  return

test35:
  say '35. The following test should issue warning 205. When it runs, it should print'
  say '    the string "Okay".'
  say ''
  say '    Comparing two constants with different values is redundant, but should'
  say '    generate valid code.'
  say '-----'
  pawncc '-v0 REDUNDANT_CODE= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'warning 205', '1 Warning.', 'Okay', '/Assertion failed'
  /* Symptoms of detected bug: invalid opcode assertion when running it. The jump
     around the following code was scrapped too.
   */
  return

test36:
  say '36. The following test should compile successfully. When it runs, it should'
  say '    print the "Result: 4".'
  say ''
  say '    The "true" and "false" expressions in a conditional operator should be'
  say '    equivalent.'
  say '-----'
  pawncc 'COND_EXPR_CONST= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'Result: 4'
  /* Symptoms of detected bug: the expression "ident" was set to the second
     expression; this caused the comparison to be optimized away if it was a
     constant expression.
   */
  return

test37:
  say '37. The following test should compile successfully. When it runs, it should'
  say '    print the "Result: 0".'
  say ''
  say '    When an argument "uses" its default value (the caller omitted the argument)'
  say '    and that argument is a const array, the compiler does not need to copy the'
  say '    default value to the heap. It can instead pass the address of the default'
  say '    value, since the function does not modify it (it is "const").'
  say '-----'
  pawncc 'DEF_ARRAY_ARG= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'Result: 0'
  /* Symptoms of detected bug: the compiler does NOT copy the default value to
     the heap, but it DOES try to remove it from the heap after the call. This
     causes a "heap underflow" error.
   */
  return

test38:
  say '38. The following test should issue error 001, expecting ":" (THREE times).'
  say ''
  say '    The colons on case statements in a switch are missing, this should be'
  say '    flagged.'
  say '-----'
  pawncc '-v0 -p SWITCH_NO_COLONS= test1' commandsuffix
  call matchoutput 'error 001', 'error 001', 'error 001', '3 Errors.'
  /* Symptoms of detected bug: the compiler hangs (it drops in an endless loop and
     does not finish compilation).
   */
  return

test39:
  say '39. The following test should compile successfully. When it runs, it does not'
  say '    print anything, but it should not drop in an assertion.'
  say ''
  say '    Note: you must have the ANSI C version of PAWNRUN to get the assertions.'
  say ''
  say '    The sample has a "for" loop with a new variable in its first expression.'
  say '    A break in the loop must not delete that variable, as the "exit code" of'
  say '    the for loop does this already.'
  say '-----'
  pawncc '-p FOR_BREAK_LCLVAR= test1' commandsuffix
  pawnrun 'test1.amx' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: an assertion for a stack mismatch (caused by a
     double delete).
   */
  return

test40:
  say '40. The following test should compile successfully. When it runs, it should'
  say '    print the values:'
  say '           p1=3.141500  p2=5.000000  p3=0.000000'
  say '           sum=8.141500'
  say '           product=15.707500'
  say '           quotient=0.628300'
  say '           negated=-0.628300'
  say ''
  say '    You must have a version of PAWNRUN that includes floating point support.'
  say '-----'
  pawncc 'USEROP_STOCK= float' commandsuffix
  pawnrun 'float.amx' commandsuffix
  call matchoutput 'Completed successfully', 'p1=3.141500  p2=5.000000  p3=0.000000', ,
          'sum=8.141500', 'product=15.707500', 'quotient=0.628300', 'negated=-0.628300'
  /* Symptoms of detected bug: stock user-defined operators were flagged as illegal
     declarations.
   */
  return

test41:
  say '41. The following test should compile successfully. When it runs, it should'
  say '    print the following (and no assertion should be fired):'
  say ''
  say '           p=30, q=30 (should be p=30, q=30)'
  say '-----'
  pawncc 'SUB_CONST_EXPR= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'p=30, q=30 (should be p=30, q=30)', '/Assertion failed'
  /* Symptoms of detected bug: p was set to -30, due to an optimizer bug.
   */
  return

test42:
  say '42. The following test should compile successfully. When it runs, it should'
  say '    print the value:'
  say '           F=3.75000'
  say ''
  say '    You must have a version of PAWNRUN that includes floating point support.'
  say '-----'
  pawncc 'USEROP_CONSTPARAMS= float' commandsuffix
  pawnrun 'float.amx' commandsuffix
  call matchoutput 'Completed successfully', 'F=3.75000'
  /* Symptoms of detected bug: due to a code generation bug when both parameters of
     user-defined operators are constants, one of these was NOT loaded into a
     register and pushed onto the stack.
   */
  return

test43:
  say '43. The following test should compile successfully (with verbose output). When'
  say '    it runs, it should print:'
  say '           Bigger'
  say '           Unequal'
  say ''
  say '    You must have a version of PAWNRUN that includes floating point support.'
  say '-----'
  pawncc '-d2 USEROP_DEBUGINFO= float' commandsuffix
  pawnrun 'float.amx' commandsuffix
  call matchoutput 'Completed successfully', 'Bigger', 'Unequal', '/Assertion failed'
  /* Symptoms of detected bug: the program compiles but crashes when run. The code
     generation fault only occurs when compiling with debug info. (It is caused by
     the symbol renaming with user-defined operators and symbol length variations).
   */
  return

test44:
  say '44. The following test should issue error 017 ("undefined symbol") TWICE.'
  say ''
  say '    A global variable is declared in the main file, but used in a file that is'
  say '    included *before* the declaration.'
  say '    The program uses the "printf" function, but does not include console.inc.'
  say '-----'
  pawncc '-v0 -p menumain' commandsuffix
  call matchoutput 'error 017', 'error 017', '2 Errors.'
  /* Symptoms of detected bug: error 004 ("function not implemented") on printf,
     no error on the variable, but the variable is not created either.
   */
  return

test45:
  say '45. The following test should compile successfully (with verbose output). When'
  say '    it runs, it should print:'
  say '           ''''a'''' okay'
  say '           ''''b'''' okay'
  say ''
  say '    The script contains an expression with chained relational operators, like'
  say '    "if (0.0 <= b <= 10.0) ...", where "<=" is a user-defined operator (in this'
  say '    case, working on "float:" tags).'
  say '-----'
  pawncc '-d2 USEROP_CHAINOP= float' commandsuffix
  pawnrun 'float.amx' commandsuffix
  call matchoutput 'Completed successfully', '''''a'''' okay', '''''b'''' okay'
  /* Symptoms of detected bug: "tag mismatch" warning, plus that the value of the
     ALT register should be saved around the call to the user-defined operator, as
     the user-defined operator is likely to clobber it.
   */
  return

test46:
  say '46. The following test should compile successfully. When it runs, it should'
  say '    print:'
  say '           result = First'
  say '           result = Second'
  say ''
  say '    The has expression a conditional operator (a ? b : c) where b and c are'
  say '    arrays (strings); this construct appears inside a printf().'
  say '-----'
  pawncc 'COND_OPER_ARRAY= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'result = First', 'result = Second'
  /* Symptoms of detected bug: the conditional operator marked the result as an
     "expression" (with a numeric value) instead of as an array. When passed to
     printf(), the address of the expression was passed instead of its value instead
     of its value (which is the address of the appropriate array).
   */
  return

test47:
  say '47. The following test should issue error 017 (undefined symbol) TWICE.'
  say ''
  say '-----'
  pawncc '-v0 test9' commandsuffix
  call matchoutput 'error 017', 'error 017', '2 Errors.'
  /* Symptoms of detected bug: in the first pass, an error is detected, but
     swallowed. The compiler then misses a synchronization point to restart
     parsing, and then sees a function call as a function declaration. This
     causes the error to go undetected in the second pass.
   */
  return

test48:
  say '48. The following test should issue error 025 (function header differs from'
  say '    prototype).'
  say ''
  say '    A function is re-declared with more parameters (the first parameters match'
  say '    the previous definition).'
  say '-----'
  pawncc '-v0 -p REDECLARE_EXPAND= test1' commandsuffix
  call matchoutput 'error 025', '1 Error.'
  /* Symptoms of detected bug: the function argument verification loop did not
     check for the number of arguments in the previous definition, and so it could
     cause a protection faulth when comparing the last (new) argument with the
     non-existing old declaration.
   */
  return

test49:
  say '49. The following test should compile successfully.'
  say ''
  say '    A stock function is never used. In its body it calls a function that is not'
  say '    implemented. This is okay, as long as the function is indeed never used.'
  say '-----'
  pawncc '-p MISSING_UNUSED_FUNC= test1' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: the compiler drops in an assertion because it assumes
     that a function symbol in its tables is either called or defined (or both). In
     the case of stock functions, the "called" bit may not get set.
   */
  return

test50:
  say '50. The following test should issue warning 209.'
  say ''
  say '    A program uses the value of a function, but the function does not return'
  say '    a value.'
  say '-----'
  pawncc '-v0 -p NO_RETURN= test1' commandsuffix
  call matchoutput 'warning 209', '1 Warning.'
  /* Symptoms of detected bug: the warning was absent when the function result was
     used in a variable declaration (initialization).
   */
  return

test51:
  say '51. The following test should issue error 021 (followed by error 010).'
  say ''
  say '    Declaring a function with the same name as an existing global variable.'
  say '-----'
  pawncc '-v0 -p REDECLARE_VAR_FUNC= test1' commandsuffix
  call matchoutput 'error 021', 'error 010', '2 Errors.'
  /* Symptoms of detected bug: the compiler crashed, because it attempted to compare
     the argument lists of the new and the old declaration (the variable does not
     have a parameter list, of course).
   */
  return

test52:
  say '52. The following TWO tests should BOTH issue warning 225 (unreachable code).'
  say ''
  say '    Code following a "return" statement at the same compound block level is'
  say '    unreachable. This exhibit of inefficient coding probably hides an error.'
  say ''
  say '    A more advanced test also checks whether both branches of an "if" statement'
  say '    end with a "return". If so, the "return" is in fact unconditional.'
  say '-----'
  pawncc '-v0 -p UNREACHABLE_CODE1= test1' commandsuffix
  pawncc '-v0 -p UNREACHABLE_CODE2= test1' commandsuffix
  call matchoutput 'warning 225', '1 Warning.', 'warning 225', '1 Warning.'
  return

test53:
  say '53. The following test should issue warning 226 TWICE (226 = self-assignment).'
  say ''
  say '    A statement like "x=x" displays inefficient coding, but it might hide an'
  say '    error. The first warning is for a simple assignment, the second for an'
  say '    array assignment.'
  say '-----'
  pawncc '-v0 -p SELF_ASSIGNMENT= test1' commandsuffix
  call matchoutput 'warning 226', 'warning 226', '2 Warnings.'
  return

test54:
  say '54. The following TWO tests should both issue error 003 (followed by others).'
  say ''
  say '    A declaration of a local variable as the only statement of a function or a'
  say '    if/for/while/do statement is useless. Due to implementation difficulties,'
  say '    it is flagged as "invalid".'
  say '-----'
  pawncc '-v0 -p USELESS_DECLARE1= test1' commandsuffix
  pawncc '-v0 -p USELESS_DECLARE2= test1' commandsuffix
  call matchoutput 'error 003', '2 Errors.', 'error 003', '2 Errors.'
  return

test55:
  say '55. The following test should compile successfully. Then, on execution, it'
  say '    should halt with run-time error 4 (bounds-check).'
  say ''
  say '    An attempt to access an array element that is just out of bounds should'
  say '    cause this run-time error.'
  say '-----'
  pawncc '-p OUT_OF_BOUNDS= test1' commandsuffix
  pawnrun 'test1.amx' commandsuffix
  call matchoutput 'Completed successfully', 'Run time error 4'
  /* Symptoms of detected bug: being 1 element beyond the array bounds was not
     flagged (e.g. "new array[10]; new x = array[10];" worked).
   */
  return

test56:
  say '56. The following test should give warning 203, but the compiler should not'
  say '    drop into an assertion.'
  say ''
  say '    This example declares an array at the bottom of the file, which is legal.'
  say '-----'
  pawncc '-v0 -p LITERAL_QUEUE= test1' commandsuffix
  call matchoutput 'warning 203', '1 Warning.', '/Assertion failed'
  /* Symptoms of detected bug: an assertion, because the literal queue was not
     cleared correctly. When a function followed the array, the literal queue was
     re-initialized, so there is no error in the common case (variables must be
     declared before use).
   */
  return

test57:
  say '57. The following test should issue error 052 and ONLY error 052.'
  say ''
  say '    2-dimensional arrays must be completely initialized (or not at all).'
  say '    Partial initialization is not allowed.'
  say '-----'
  pawncc '-v0 INCOMPLETE_2D_ARRAY= test10' commandsuffix
  call matchoutput 'error 052', '1 Error.'
  /* Symptoms of detected bug: after detecting the error, the compiler proceded with
     trying to find expressions that are initializers of a sub-array. This may end
     up "eating" part of other declarations or functions, or attempting to read past
     the end of file.
   */
  return

test58:
  say '58. The following test should compile successfully. On execution, it should'
  say '    print:'
  say ''
  say '       Before: 0'
  say '       After:  1'
  say ''
  say '    An variable that is passed to printf() and post-incremented (for example'
  say '    printf("%d", a++)) must pass the old value, not the new value.'
  say '-----'
  pawncc 'POST_INCREMENT_REF= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'Before: 0', 'After:  1'
  /* Symptoms of detected bug: the compiler passed the address of the variable, in
     this particular case an incorrect optimization because the post-incremented
     variable is no longer an lvalue.
   */
  return

test59:
  say '59. The following TWO tests should both issue error 032.'
  say ''
  say '    A negative array index is always out of bounds, whether it is used in an'
  say '    assignment or passed as a parameter.'
  say '-----'
  pawncc '-v0 NEGATIVE_INDEX1= test2' commandsuffix
  pawncc '-v0 NEGATIVE_INDEX2= test2' commandsuffix
  call matchoutput 'error 032', '1 Error.', 'error 032', '1 Error.'
  /* Symptoms of detected bug: the compiler did not catch this bug.
   */
  return

test60:
  say '60. The following TWO tests should both compile successfully, but drop into'
  say '    run-time error 4 ("out-of-bounds") when run'
  say ''
  say '    A negative array index is always out of bounds, whether it is used in an'
  say '    assignment or passed as a parameter.'
  say '-----'
  pawncc 'NEGATIVE_INDEX3= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  pawncc 'NEGATIVE_INDEX4= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'Run time error 4', 'Completed successfully', 'Run time error 4'
  /* Symptoms of detected bug: the run-time did not catch all instances of this bug.
     Note: when you compile without run-time checks (compiler option -d0), you
     should still get run-time error 5 ("memory access").
   */
  return

test61:
  say '61. The following test should compile successfully. When run, compare the'
  say '    results with those printed in parantheses behind every result.'
  say ''
  say '    Note: the "off-by-0.001" error in raising to the 3rd power is a known'
  say '    problem that can only be fixed by using internal calculations with more'
  say '    than 3 decimals.'
  say '-----'
  pawncc 'tstfixed' commandsuffix
  pawnrun 'tstfixed.amx' commandsuffix
  call matchoutput 'Completed successfully', ,
          'Value is 3.142 (should be 3.142)', ,
          '1.000 / 3.000 = 0.333 (should be 1.000 / 3.000 = 0.333)', ,
          '2.000 / 3.000 = 0.667 (should be 2.000 / 3.000 = 0.667)', ,
          '2.0 / 3.000 = 0.667 (should be 2.0 / 3.000 = 0.667)', ,
          '2 / 3.000 = 0.667 (should be 2 / 3.000 = 0.667)', ,
          '2.000 / 3 = 0.667 (should be 2.000 / 3 = 0.667)', ,
          '3.297 * 2.426 = 7.999 (should be 3.297 * 2.426 = 7.999)', ,
          '3.142 ^ 2 = 9.872 (should be 3.142 ^ 2 = 9.872)', ,
          '3.142 ^ 3 = 31.019 (should be 3.142 ^ 3 = 31.018)', ,
          '3.142 ^ 4 = 97.460 (should be 3.142 ^ 4 = 97.460)', ,
          '3.142 ^ 0 = 1.000 (should be 3.142 ^ 0 = 1.000)', ,
          '3.142 ^ -1 = 0.318 (should be 3.142 ^ -1 = 0.318)', ,
          '3.142 ^ -2 = 0.101 (should be 3.142 ^ -2 = 0.101)', ,
          'sqroot(2) = 1.414 (should be 1.414)', ,
          'sqroot(3) = 1.732 (should be 1.732)', ,
          'sqroot(7) = 2.646 (should be 2.646)'

  return

test62:
  say '62. The following test should compile successfully.'
  say ''
  say '    Self-assignment is not the case when an unary operator is applied.'
  say '-----'
  pawncc '-p NO_SELF_ASSIGNMENT= test1' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: warning 226 on expressions like "var = !var".
   */
  return

test63:
  say '63. The following test should give error 021, but WITHOUT memory leakage report.'
  say ''
  say '    The compiler must use FORTIFY for memory leakage detection.'
  say '-----'
  pawncc '-v0 DOUBLE_NATIVE_DECLARE= test2' commandsuffix
  call matchoutput 'error 021', '/Fortify: Memory List'
  /* Symptoms of detected bug: A double declaration of a native function caused
     arguments to be allocated another time, as the prototype flag was cleared.
   */
  return

test64:
  say '64. The following test should compile successfully. When run, it should print:'
  say '      Eenie, Meenie, Meinie, Moe'
  say '      Out Goes You'
  say ''
  say '    Tests for 2-dimensional arrays, where both the major and the minor'
  say '    dimensions are unspecified.'
  say '-----'
  pawncc '-p array' commandsuffix
  pawnrun 'array.amx' commandsuffix
  call matchoutput 'Completed successfully', 'Eenie, Meenie, Meinie, Moe', 'Out Goes You', '/Assertion failed'
  return

test65:
  say '65. The following test should compile successfully. When run, it should print:'
  say '      Eenie, Meenie, Meinie, Moe'
  say '      Out Goes You'
  say ''
  say '    Basically the same test as the earlier release, but now with a mix of'
  say '    specified and non-specified dimensions.'
  say '-----'
  pawncc '-p MIX_DIMENSIONS= array' commandsuffix
  pawnrun 'array.amx' commandsuffix
  call matchoutput 'Completed successfully', 'Eenie, Meenie, Meinie, Moe', 'Out Goes You', '/Assertion failed'
  /* Symptoms of detected bug: the compiler dropped into an assertion.
   */
  return

test66:
  say '66. The following test should compile successfully.'
  say ''
  say '    Calling a function before it is defined is valid in a 2-pass compiler.'
  say '-----'
  pawncc 'CALL_BEFORE_DEF= test2' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: an assertion due to a parsing error after picking up
     the undefined function.
   */
  return

test67:
  say '67. The following FOUR tests should compile successfully. When run, each should'
  say '    print the lines:'
  say '        1,1,1,1|'
  say '        1,1,1,1|2,2,2,2|3,3,3,3|'
  say '        1,1,1,1|2,2,2,2|3,3,3,3|10,10,10,10|20,20,20,20|30,30,30,30|'
  say '-----'
  pawncc 'DIMxxx= array3d' commandsuffix
  pawnrun 'array3d.amx' commandsuffix
  pawncc 'DIMxx_= array3d' commandsuffix
  pawnrun 'array3d.amx' commandsuffix
  pawncc 'DIMx__= array3d' commandsuffix
  pawnrun 'array3d.amx' commandsuffix
  pawncc 'DIM___= array3d' commandsuffix
  pawnrun 'array3d.amx' commandsuffix
  call matchoutput 'Completed successfully', '1,1,1,1|', '1,1,1,1|2,2,2,2|3,3,3,3|', '1,1,1,1|2,2,2,2|3,3,3,3|10,10,10,10|20,20,20,20|30,30,30,30|', ,
          'Completed successfully', '1,1,1,1|', '1,1,1,1|2,2,2,2|3,3,3,3|', '1,1,1,1|2,2,2,2|3,3,3,3|10,10,10,10|20,20,20,20|30,30,30,30|', ,
          'Completed successfully', '1,1,1,1|', '1,1,1,1|2,2,2,2|3,3,3,3|', '1,1,1,1|2,2,2,2|3,3,3,3|10,10,10,10|20,20,20,20|30,30,30,30|', ,
          'Completed successfully', '1,1,1,1|', '1,1,1,1|2,2,2,2|3,3,3,3|', '1,1,1,1|2,2,2,2|3,3,3,3|10,10,10,10|20,20,20,20|30,30,30,30|'
  return

test68:
  say '68. The following test should compile successfully.'
  say ''
  say '    The preprocessor should run over expressions in compiler directives; in'
  say '    parsing for text substitutions, omit substituting a symbol that follows the'
  say '    keyword "defined", as in "#if defined TESTMODE".'
  say '-----'
  pawncc '-p PREPROCESS_DIRECTIVES= test1' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: an expression like "#if TESTMODE > 2" gave an error,
     because a "#define" does not put the name TESTMODE in the symbol table.
   */
  return

test69:
  say '69. The following test should compile successfully.'
  say ''
  say '    Various tests on expressions using the conditional operator.'
  say '-----'
  pawncc '-p TAGS_IN_COND_OPER_OK= test1' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: the trailing colon of a tag name was mis-interpretted
     as the colon of the conditional operator. The new syntax requires such
     ambiguous expression to be contained in parantheses.
   */
  return

test70:
  say '70. The following test should issue error 81 (followed by more errors).'
  say ''
  say '    Using sub-expressions with tag overrides inside a conditional operator.'
  say '-----'
  pawncc '-v0 -p TAGS_IN_COND_OPER_WRONG= test1' commandsuffix
  call matchoutput 'error 081', '2 Errors.'
  /* Symptoms of detected bug: the trailing colon of a tag name was mis-interpreted
     as the colon of the conditional operator. The new syntax requires such
     ambiguous expression to be contained in parantheses.
   */
  return

test71:
  say '71. The following test should compile successfully; when run, it should print:'
  say ''
  say '    result = 1'
  say '-----'
  pawncc 'test_glbvar' commandsuffix
  pawnrun 'test_glbvar.amx' commandsuffix
  call matchoutput 'Completed successfully', 'result = 1'
  /* Symptoms of detected bug: wrong output due to code generation bug. The issue is
     a global variable that is passed to forward function (and never modified), and
     which was then removed as if it weren''t used.
   */
  return

test72:
  say '72. The following test should give error 047.'
  say ''
  say '    Arrays declared with symbolic indices are not "coercible" in assignments.'
  say '-----'
  pawncc '-v0 ARRAY_INDEX_TAG_ASSIGN= test11' commandsuffix
  call matchoutput 'error 047', '1 Error.'
  return

test73:
  say '73. The following test should give error 047.'
  say ''
  say '    Arrays declared with symbolic indices are not "coercible" in function calls.'
  say '-----'
  pawncc '-v0 ARRAY_INDEX_TAG_CALL= test11' commandsuffix
  call matchoutput 'error 047', '1 Error.'
  return

test74:
  say '74. The following test should give error 025.'
  say ''
  say '    Symbolic indices of array arguments indices should be checked between'
  say '    declaration and definition.'
  say '-----'
  pawncc '-v0 ARRAY_INDEX_TAG_DECL= test11' commandsuffix
  call matchoutput 'error 025', '1 Error.'
  return

test75:
  say '75. The following test should compile successfully.'
  say ''
  say '    When copying an array into a symbolic array field, the index checking'
  say '    should be relaxed.'
  say '-----'
  pawncc 'ARRAY_INDEX_TAG_ASSIGN2= test11' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: warning 229, which could not be avoided by whatever
     tag override you could think of.
   */
  return

test76:
  say '76. The following test should compile successfully. When run, it should print:'
  say ''
  say '        one (should be one)'
  say '        exit = 2 (should be 2)'
  say ''
  say '    The expression between the parentheses of a switch stament has a side'
  say '    effect (like in "switch (a++) { ... }").'
  say '-----'
  pawncc 'SWITCH_SIDE_EFFECT= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'one (should be one)', 'exit = 2 (should be 2)'
  /* Symptoms of detected bug: When the expression between the parentheses of a
     switch stament had a side effect, part of the expression could be optimized
     out. This was due to the switch marking an "end of expression".
   */
  return

test77:
  say '77. The following test should compile successfully. When run, it should print:'
  say ''
  say '        5 5 5'
  say ''
  say '    Chained assignment of array elements (a[0] = a[1] = a[2] = 5).'
  say '-----'
  pawncc 'CHAINED_ARRAY_ASSIGN= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', '5 5 5', '/Assertion failed'
  /* Symptoms of detected bug: The compiler failed in an assertion.
   */
  return

test78:
  say '78. The following test should compile successfully.'
  say ''
  say '    Copy one array item into another, where one of these is a constant.'
  say '-----'
  pawncc '-p ARRAY_CELL_ASSIGN_NOSELF= test1' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: The compiler issued warning 226 (self-assignment).
   */
  return

test79:
  say '79. The following test should compile successfully.'
  say ''
  say '    Combinations of "sizeof" and symbolic array subscripts.'
  say '-----'
  pawncc '-p SIZEOF_PSEUDO_ARRAY= test1' commandsuffix
  call matchoutput 'Completed successfully'
  return

test80:
  say '80. The following test should compile successfully.'
  say ''
  say '    A constant (enumeration) passed to a function with variable arguments.'
  say '-----'
  pawncc 'CONST_WRITE_USAGE= test2' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: The compiler crashed. Functions that accept variable
     arguments get those arguments "by reference". When it is a constant, it is
     copied to the heap. The "variable is written" usage flag is then also set. This
     flag overlapped with the "variable is an enumeration root" flag.
   */
  return

test81:
  say '81. The following test should compile successfully.'
  say ''
  say '    A long symbol name (26 characters) for a local variable.'
  say '-----'
  pawncc 'SYMBOL_TOO_LONG_FOR_OPTIMIZER= test2' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: Although the compiler accepts symbol names up to 32
     characters, the peephole optimizer still had a test for a shorter name. The
     compiler dropped into an assertion for names over 21 characters.
   */
  return

test82:
  say '82. The following test should compile successfully. When running, it must print'
  say '    9 lines with "Hello world"'
  say ''
  say '    Several tests for string functions (insertion, deletion, selection, ...).'
  say '-----'
  pawncc 'strtst1' commandsuffix
  pawnrun 'strtst1' commandsuffix
  call matchoutput 'Completed successfully', '1. Hello world', '2. Hello world', '3. Hello world', ,
          '4. Hello world', '5. Hello world', '6. Hello world', '7. Hello world', '8. Hello world', ,
          '9. Hello world'
  return

test83:
  say '83. The following test should compile successfully. When running, it must print'
  say '    lines where the value beginning each line must match the one following it.'
  say ''
  say '    More tests for string functions (number conversions).'
  say '-----'
  pawncc 'strtst2' commandsuffix
  pawnrun 'strtst2' commandsuffix
  call matchoutput 'Completed successfully', '0 (should be 0)', '9 (should be 9)', ,
          '10 (should be 10)', '123 (should be 123)', '-9 (should be -9)', '-10 (should be -10)', ,
          '-123 (should be -123)', '0 (should be 0)', '9 (should be 9)', '10 (should be 10)', ,
          '123 (should be 123)', '-9 (should be -9)', '-10 (should be -10)', '-123 (should be -123)'
  return

test84:
  say '84. The following test should compile successfully. When running, it does not'
  say '    print anything, but it should not drop into an assertion.'
  say ''
  say '    Even more tests for string functions (searching and comparing).'
  say '-----'
  pawncc 'strtst3' commandsuffix
  pawnrun 'strtst3' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  return

test85:
  say '85. The following test should compile successfully. When running, it does not'
  say '    print anything, but it should not drop into an assertion.'
  say ''
  say '    A 3-dimensional array with a variable last dimension.'
  say '-----'
  pawncc '-p MULTIDIM_ARRAY_VARDIM= test1' commandsuffix
  pawnrun 'test1' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: The indirection table for the second level was copied
     over all instances of the second level, instead of being recalculated.
   */
  return

test86:
  say '86. The following test should issue error 052.'
  say ''
  say '    A 2-dimensional array with a variable last dimension and an incomplete'
  say '    initialization.'
  say '-----'
  pawncc '-v0 -p ARRAY_VARDIM_INCOMPLETE= test1' commandsuffix
  call matchoutput 'error 052', '1 Error.', '/Assertion failed'
  /* Symptoms of detected bug: The error was detected, but the compiler dropped in
     an assertion when proceeding to initialize the indirection tables.
   */
  return

test87:
  say '87. The following test should issue error 009 (followed by more errors).'
  say ''
  say '    A 2-dimensional array with a variable last dimension and an absent'
  say '    initialization.'
  say '-----'
  pawncc '-v0 -p ARRAY_VARDIM_NONINIT= test1' commandsuffix
  call matchoutput 'error 009', '3 Errors.', '/Assertion failed'
  /* Symptoms of detected bug: The error was detected, but the compiler dropped in
     an assertion when proceeding to initialize the indirection tables.
   */
  return

test88:
  say '88. The following test should issue error 001 (followed by more errors).'
  say ''
  say '    A multi-dimensional array is initialized with {...} syntax for the major'
  say '    dimension.'
  say '-----'
  pawncc '-v0 -p ARRAY_VARDIM_WRONGBRACKETS= test1' commandsuffix
  call matchoutput 'error 001', '2 Errors.'
  return

test89:
  say '89. The following test should compile successfully. When running, it must'
  say '    print:'
  say ''
  say '        hello'
  say '-----'
  pawncc 'MULTI_DIM_PARTIAL_COUNT= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'hello'
  /* Symptoms of detected bug: No or incorrect text printed.
   */
  return

test90:
  say '90. The following test should compile successfully. When running, it must print'
  say '    the lines:'
  say '         7: 1abcdef'
  say '         7: 12abcde'
  say '         7: 123abcd'
  say '         7: 1234abc'
  say '         11: 1abcdefghij'
  say '         12: 12abcdefghij'
  say '         13: 123abcdefghij'
  say '         14: 1234abcdefghij'
  say '-----'
  pawncc 'strtst4' commandsuffix
  pawnrun 'strtst4' commandsuffix
  call matchoutput 'Completed successfully', '7: 1abcdef', '7: 12abcde', '7: 123abcd', ,
          '7: 1234abc', '11: 1abcdefghij', '12: 12abcdefghij', '13: 123abcdefghij', ,
          '14: 1234abcdefghij'
  return

test91:
  say '91. The following test should compile successfully.'
  say ''
  say '    Named automatons (instead of the default).'
  say '-----'
  pawncc 'NAMED_AUTOMATON= states' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: errors due to the parser not picking up the code
     correctly after seeing an automaton being used before declaration (in the
     scanning phase).
   */
  return

test92:
  say '92. The following TWO tests should BOTH issue error 088.'
  say ''
  say '    An attempt to declare public and local state variables.'
  say '-----'
  pawncc '-v0 PUBLIC_STATE_VAR= test1' commandsuffix
  pawncc '-v0 LOCAL_STATE_VAR= test1' commandsuffix
  call matchoutput 'error 088', '1 Error.', 'error 088', '1 Error.'
  return

test93:
  say '93. The following test should issue error 089 (followed by warning 204).'
  say ''
  say '    An attempt to declare a state variable with an initialler.'
  say '-----'
  pawncc '-v0 INITIALIZED_STATE_VAR= test1' commandsuffix
  call matchoutput 'error 089', '1 Error.'
  return

test94:
  say '94. The following test should issue error 017 but NOT warning 219.'
  say ''
  say '    A function uses a local variable which it does not declare (error 017).'
  say '    Another function has a local variable with the same name. This is okay.'
  say '-----'
  pawncc '-v0 WRONG_PROC_CALL= test13' commandsuffix
  call matchoutput 'error 017', '1 Error.', '/warning 219'
  /* Symptoms of detected bug: in a special syntax (procedure call), the undeclared
     variable was interpreted as a function (use before declaration).
   */
  return

test95:
  say '95. The following test should compile successfully.'
  say ''
  say '    A script using an include file, where the include file contains both a'
  say '    static and a stock function.'
  say '-----'
  pawncc 'mainpgm' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: a compiler crash, because the assembler could not
     find the static function (wrong file number).
   */
  return

test96:
  say '96. The following test should issue warning 235.'
  say ''
  say '    A public function is not prototyped. This may indicate that the user typed'
  say '    the wrong name for the public function (so this is a warning for a common'
  say '    error.'
  say '-----'
  pawncc '-v0 UNDECLARED_PUBLIC= test1' commandsuffix
  call matchoutput 'warning 235', '1 Warning.'
  return

test97:
  say '97. The following test should compile successfully (without memory log).'
  say ''
  say '    A script predefining several (unused) public functions.'
  say '-----'
  pawncc 'PROTOTYPED_PUBLIC= test1' commandsuffix
  call matchoutput 'Completed successfully', '/Fortify: Memory List'
  /* Symptoms of detected bug: a memory leak, because the tags of the forward
     declaration were not removed after unflagging the public function as
     prototyped.
   */
  return

test98:
  say '98. The following test should issue error 033 (array is not indexed).'
  say ''
  say '    Arrays in a logical expression, with && and ||, must always by fully.'
  say '    indexed.'
  say '-----'
  pawncc '-v0 ARRAY_LOGIC_EXPR= test1' commandsuffix
  call matchoutput 'error 033', '1 Error.'
  /* Symptoms of detected bug: this was not checked by the compiler.
   */
  return

test99:
  say '99. The following test should compile successfully. When running, it should'
  say '     print:'
  say ''
  say '         Passed a Sentient 2'
  say '         Passed a Foo 1'
  say '         Passed something else 0'
  say ''
  say '     The tagof operator with functions accepting multiple tags.'
  say '-----'
  pawncc 'TAGOF_PARAM= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'Passed a Sentient 2', 'Passed a Foo 1', 'Passed something else 0'
  /* Symptoms of detected bug: tag was taken from a variable, not of an expression.
   */
  return

test100:
  say '100. The following test should issue error 009 (invalid array size).'
  say ''
  say '     An array that exceeds 4 GiB.'
  say '-----'
  pawncc '-v0 ARRAY_TOO_LARGE= test1' commandsuffix
  call matchoutput 'error 009', '1 Error.'
  /* Symptoms of detected bug: no size check; invalid code generation.
   */
  return

test101:
  say '101. The following test should issue warning 235 TWICE (missing forward'
  say '     declaration).'
  say ''
  say '     A public function calls another public function; there are no forward.'
  say '     declarations for the public functions.'
  say '-----'
  pawncc '-v0 INTERNAL_PUBLIC_CALL= test14' commandsuffix
  call matchoutput 'warning 235', 'warning 235', '2 Warnings.'
  /* Symptoms of detected bug: compiler crashed.
   */
  return

test102:
  say '102. The following test should issue warning 201 TWICE (redefined symbol).'
  say ''
  say '     Redefinition of constants.'
  say '-----'
  pawncc '-v0 REDEFINE_CONSTANTS= test1' commandsuffix
  call matchoutput 'warning 201', 'warning 201', '2 Warnings.'
  /* Symptoms of detected bug: redefinition was silently allowed.
   */
  return

test103:
  say '103. The following test should compile successfully (NO warnings 213).'
  say ''
  say '     Symbolic array indices with tag overrides.'
  say '-----'
  pawncc 'TAGGED_SYMBOLIC_INDEX= rational' commandsuffix
  call matchoutput 'Completed successfully'
  return

test104:
  say '104. The following TWO tests should BOTH compile successfully (NO error 38).'
  say ''
  say '     #elseif with an expression.'
  say '-----'
  pawncc 'ELSEIF_EXPR=1 test1' commandsuffix
  pawncc 'ELSEIF_EXPR=2 test1' commandsuffix
  call matchoutput 'Completed successfully', 'Completed successfully'
  /* Symptoms of detected bug: #elseif did not parse through the expression when it
     had already found a "true" case; it should parse and ignore the expression.
   */
  return

test105:
  say '105. The following test should issue warning 236. When running, it should'
  say '     print:'
  say ''
  say '        %1test'
  say ''
  say '     A pre-processor macro that contains a % in a string, which should be'
  say '     copied literally.'
  say '-----'
  pawncc 'MACRO_PARM_INSTR= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'warning 236', '1 Warning.', '%1test'
  /* Symptoms of detected bug: the parameter was replaced (by an empty string).
   */
  return

test106:
  say '106. The following test should issue warning 236 (followed by errors).'
  say ''
  say '     A pre-processor uses a parameter in replacement that is not in the source.'
  say '-----'
  pawncc '-v0 MACRO_PARM_UNKNOWN= test2' commandsuffix
  call matchoutput 'warning 236', 'error 029', '1 Error.'
  /* Symptoms of detected bug: the parameter would replaced by an empty string,
     without warning.
   */
  return

test107:
  say '107. The following test should compile successfully. When running, it should'
  say '     print:'
  say ''
  say '        (+8,+3)  q=+2 r=+2'
  say '        (+8,-3)  q=-3 r=-1'
  say '        (-8,+3)  q=-3 r=+1'
  say '        (-8,-3)  q=+2 r=-2'
  say '        (+9,+3)  q=+3 r=+0'
  say '        (+9,-3)  q=-3 r=+0'
  say '        (-9,+3)  q=-3 r=+0'
  say '        (-9,-3)  q=+3 r=+0'
  say ''
  say '     Tests for floored division.'
  say '-----'
  pawncc 'FLOORED_DIVISION= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', '(+8,+3)*q=+2 r=+2', '(+8,-3)*q=-3 r=-1', ,
          '(-8,+3)*q=-3 r=+1', '(-8,-3)*q=+2 r=-2', '(+9,+3)*q=+3 r=+0', ,
          '(+9,-3)*q=-3 r=+0', '(-9,+3)*q=-3 r=+0', '(-9,-3)*q=+3 r=+0'
  return

test108:
  say '108. The following test should issue warning 225 (unreachable code).'
  say ''
  say '    Code below an infinite loop, which does not contain a "break".'
  say '-----'
  pawncc '-v0 -p UNREACHABLE_CODE3= test1' commandsuffix
  call matchoutput 'warning 225', '1 Warning.'
  return

test109:
  say '109. The following test should issue error 001 (expected an identifier instead'
  say '     of "new"), followed by other errors'
  say ''
  say '     You cannot mix expressions and variable declarations in a "for" loop.'
  say '-----'
  pawncc '-v0 FOR_MIX_EXPR_VAR= test1' commandsuffix
  call matchoutput 'error 001*''<identifier>''*''new''', '4 Errors.'
  /* Symptoms of detected bug: obscure error messages.
   */
  return

test110:
  say '110. The following test should issue warning 215'
  say ''
  say '     With optional parentheses, expressions may become ambiguous.'
  say '-----'
  pawncc '-v0 AMBIGUOUS_CALL= test1' commandsuffix
  call matchoutput 'warning 215', '1 Warning.'
  /* Symptoms of detected bug: obscure error message followed by a crash.
   */
  return

test111:
  say '111. The following test should compile successfully; when run, it should print'
  say '     the value 2'
  say ''
  say '     Using the prefix increment operator in a return/exit/sleep statement.'
  say '-----'
  pawncc 'RETURN_WITH_INCR= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', '2'
  /* Symptoms of detected bug: incorrect optimization.
   */
  return

test112:
  say '112. The following test should compile successfully; when run, it should print:'
  say ''
  say '         result=123'
  say '         result=1'
  say ''
  say '     Ternary operator combined with functions returning arrays.'
  say '-----'
  pawncc 'COND_EXP_ARRAY= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'result=123', 'result=1'
  /* Symptoms of detected bug: heap underflow, because heap was restored for either.
     branch, while only one branch is taken.
   */
  return

test113:
  say '113. The following test should compile successfully; when run, it should print'
  say ''
  say '         queue: 15%'
  say '         queue:'
  say ''
  say '     before dropping into run-time error 10 (native function failed)'
  say ''
  say '     The native printf function did not support %% correctly. The run-time'
  say '     error in this script is correct because the second printf lacks the'
  say '     parameter.'
  say '-----'
  pawncc 'PRINTF_PCT= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'queue: 15%', 'queue:', 'Run time error 10'
  /* Symptoms of detected bug: heap underflow, because heap was restored for either.
     branch, while only one branch is taken.
   */
  return

test114:
  say '114. The following test should issue warning 237 THREE times'
  say ''
  say '     Detection and notification of recursion (both direct and indirect).'
  say '-----'
  pawncc '-v0 RECURSION_DETECTION= -v test1' commandsuffix
  call matchoutput 'warning 237', 'warning 237', 'warning 237', '3 Warnings.'
  return

test115:
  say '115. The following test should compile successfully and run without errors'
  say '     (there is no output).'
  say ''
  say '     Chained assignement (a = b = 3)'
  say '-----'
  pawncc 'CHAINED_ASSIGN= -O2 test1' commandsuffix
  pawnrun 'test1.amx' commandsuffix
  call matchoutput 'Completed successfully', '/Run time error*Assertion failed'
  /* Symptoms of detected bug: one of the variables set to a random value, due to
     a bug in one of the rules of the peephole optimizer.
   */
  return

test116:
  say '116. The following test should issue error 009 (invalid array size).'
  say ''
  say '     Declaring a partial cell size for an array, e.g.: Array[0.5]'
  say '-----'
  pawncc '-v0 PARTIAL_ARRAY_SIZE= test2' commandsuffix
  call matchoutput 'error 009', '1 Error.'
  /* Symptoms of detected bug: no error message; the compiler created a huge literal
     pool for the array.
   */
  return

test117:
  say '117. The following test should compile successfully; when run, it should print'
  say ''
  say '         size: 2 x 3'
  say ''
  say '     Declaring an array with a zero-sized major dimension and initiallers'
  say '-----'
  pawncc 'COUNT_ARRAY_SIZE= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'size: 2 x 3'
  /* Symptoms of detected bug: incorrect size determination by the compiler.
   */
  return

test118:
  say '118. The following test should issue error 054 (unmatched closing brace).'
  say ''
  say '     Unbalanced braces terminate a function in the middle.'
  say '-----'
  pawncc '-v0 UNBALANCED_BRACES= test2' commandsuffix
  call matchoutput 'error 054', '1 Error.', '/Assertion failed'
  /* Symptoms of detected bug: an assertion in the maintenance of the stack usage
     of the function.
   */
  return

test119:
  say '119. The following test should issue warning 203 (unused symbol) and report it'
  say '     in the file "menu.inc" (not in "test2.p").'
  say ''
  say '     An unused variable is declared in an include file; the error message'
  say '     should report the correct filename and line number.'
  say '-----'
  pawncc '-v0 DECLARATION_POSITION= test2' commandsuffix
  call matchoutput 'menu.inc*warning 203', '1 Warning.'
  /* Symptoms of detected bug: the issue was reported in the main file, not the
     include file in which it occurred.
   */
  return

test120:
  say '120. The following test should compile successfully.'
  say ''
  say '     Native function returning a (packed) array.'
  say '-----'
  pawncc 'NATIVE_RET_ARRAY= test2' commandsuffix
  /*??? should also run this example, but need a native function that returns an array */
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: assertion failures in the compiler.
   */
  return

test121:
  say '121. The following test should compile successfully; when run, it should print'
  say ''
  say '         111 112 113 114 - 121 122 123 124 - 131 132 133 134 -'
  say '          11  12  13  14 -  21  22  23  24 -  31  32  33  34 -'
  say ''
  say '     Assigning a 2-dimensional array to a slot in a 3-dimensional array.'
  say '-----'
  pawncc 'ARRAY2DTO3D= array2' commandsuffix
  pawnrun 'array2.amx' commandsuffix
  call matchoutput 'Completed successfully', '111 112 113 114 - 121 122 123 124 - 131 132 133 134 -', '11  12  13  14 -  21  22  23  24 -  31  32  33  34 -'
  /* Symptoms of detected bug: incorrect print-out, due to overwriting the index
     vector.
   */
  return

test122:
  say '122. The following test should compile successfully.'
  say ''
  say '     Several declarations of enumerated constants declarations (optional'
  say '     terminating commas).'
  say '-----'
  pawncc 'ENUM_GOOD_DECL= test1' commandsuffix
  call matchoutput 'Completed successfully'
  return

test123:
  say '123. The following test should issue error 001, expecting "}" but finding an'
  say '     identifier.'
  say ''
  say '     Omitting commas in an enumeration declaration (where they are required).'
  say '-----'
  pawncc '-v0 ENUM_BAD_DECL= test1' commandsuffix
  call matchoutput 'error 001*expected ''}''', '1 Error.'
  return

test124:
  say '124. The following test should compile successfully; when run, it should print:'
  say ''
  say '         Eenie Meenie'
  say '         Meinie Moe'
  say ''
  say '     Concatenation of string literals.'
  say '-----'
  pawncc 'LIT_STRING_CAT= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'Eenie Meenie', 'Meinie Moe'
  return

test125:
  say '125. The following test should issue warning 238 TWICE; when run, it should'
  say '     print:'
  say ''
  say '         Eenie Meenie'
  say '         Meinie Moe'
  say ''
  say '     Concatenation of string literals, but mixing string formats.'
  say '-----'
  pawncc '-v0 LIT_STRING_CAT_MIX= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'warning 238', 'warning 238', 'Eenie Meenie', 'Meinie Moe'
  return

test126:
  say '126. The following test should issue error 025.'
  say ''
  say '     A non-public function that is forward declared as public.'
  say '-----'
  pawncc '-v0 PUBLIC_NOT_DECLARED= test1' commandsuffix
  call matchoutput 'error 025', '1 Error.'
  return

test127:
  say '127. The following test should compile successfully.'
  say ''
  say '     A public function that is forward declared as non-public. This is silently'
  say '     allowed (the function is marked as public)'
  say '-----'
  pawncc 'PUBLIC_NOT_FORWARDED= test1' commandsuffix
  call matchoutput 'Completed successfully'
  return

test128:
  say '128. The following test should compile successfully.'
  say ''
  say '     A double backslash as the only element in a string should be ok.'
  say '-----'
  pawncc 'STRING_DBL_ESCAPE= test1' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: error 037.
   */
  return

test129:
  say '129. The following test should compile successfully; when run, it should print:'
  say ''
  say '         -1, -1, -1, -1'
  say ''
  say '     Array declaration based on an enum and using an ellipsis.'
  say '-----'
  pawncc 'ENUM_ELLIPSIS= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', '-1, -1, -1, -1'
  /* Symptoms of detected bug: zero-padding, due to sequence variables for the
     ellipsis being reset in the code snippet handling enum sub-fields.
   */
  return

test130:
  say '130. The following test should compile successfully; when run, it should print:'
  say ''
  say '         logging: test'
  say ''
  say '     Stringize operator.'
  say '-----'
  pawncc 'STRINGIZE_OPER= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'logging: test'
  return

test131:
  say '131. The following TWO tests should BOTH issue error 047 (array size mismatch).'
  say ''
  say '     Conditional (ternary) operator where both rvalues are 1D arrays with'
  say '     different sizes.'
  say '-----'
  pawncc '-v0 TERNARY_LIT_ARRAY= test1' commandsuffix
  pawncc '-v0 TERNARY_1D_ARRAY= test1' commandsuffix
  call matchoutput 'error 047', '1 Error.', 'error 047', '1 Error.'
  /* Symptoms of detected bug: no error.
   */
  return

test132:
  say '132. The following test should compile successfully; when run, it should print:'
  say ''
  say '         32767'
  say '         32768'
  say '         -32767'
  say '         -32768'
  say '         -32769'
  say ''
  say '     Limiting values for the criterion for storing as a packed opcode.'
  say '     different sizes.'
  say '-----'
  pawncc '-O:2 PACKED_OPCODE_LIMITS= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', '32767', '32768', '-32767', '-32768', '-32769'
  /* Symptoms of detected bug: wrong limits were applied; a parameter could be put in
     a packed opcode while it did not fit in one.
   */
  return

test133:
  say '133. The following test should issue error 001, expecting a "]".'
  say ''
  say '     An array of strings where the strings are not separated with commas.'
  say '-----'
  pawncc '-v0 ARRAY2D_NO_COMMA= test1' commandsuffix
  call matchoutput 'error 001*expected '']''', '1 Error.', '/Assertion failed'
  /* Symptoms of detected bug: an assertion in the compiler.
   */
  return

test134:
  say '134. The following test should compile successfully; when run, it should print:'
  say ''
  say '         Result = 1'
  say ''
  say '     Chained relational expression with constant values (e.g. "1 < 2 < 3").'
  say '-----'
  pawncc 'CONST_REL_CHAINED_OP= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'Result = 1'
  /* Symptoms of detected bug: all expressions of such kind were evaluated as 0.
   */
  return

test135:
  say '135. The following test should compile successfully.'
  say ''
  say '     A public function with parameters is valid.'
  say '-----'
  pawncc 'PUBLIC_WITH_PARAMETERS= test1' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: error 25, due to an uninitialized variable.
     This bug surfaced in Linux only.
   */
  return

test136:
  say '136. The following test should issue warnings 215 and 217, but NOT error 028.'
  say ''
  say '-----'
  pawncc '-v0 ELSE_NO_IF= test1' commandsuffix
  call matchoutput 'warning 215', 'warning 217', '/error 028'
  /* Symptoms of detected bug: error 28, due to braces being misinterpreted as an
     array index.
   */
  return

test137:
  say '137. The following test should issue error 008.'
  say ''
  say '     A global variable initialized by a function call.'
  say '-----'
  pawncc '-v0 INVALID_GLOBAL_DECL= test1' commandsuffix
  call matchoutput 'error 008', '1 Error.', '/Assertion failed'
  /* Symptoms of detected bug: assertion if the function call uses a string literal
     and errors about unknown functions otherwise.
   */
  return

test138:
  say '138. The following test should issue error 001, expecting token: "[", but '
  say '     finding "{" (followed by more errors).'
  say ''
  say '     A local static array with an incorrect (packed/unpacked) initialization.'
  say '-----'
  pawncc '-v0 INVALID_STATIC_2D_PACKED= test1' commandsuffix
  call matchoutput 'error 001*expected ''[''', '4 Errors.', '/Assertion failed'
  /* Symptoms of detected bug: assertion because the literal queue was cleared on
     the parsing error and the indirection tables relied on data still being there.
   */
  return

test139:
  say '139. The following test should issue warning 229 TWICE.'
  say ''
  say '     Mismatching index operators on packed and unpacked arrays.'
  say '-----'
  pawncc '-v0 MIX_PACK_ASSIGN= test2' commandsuffix
  call matchoutput 'warning 229', 'warning 229'
  return

test140:
  say '140. The following test should compile successfully; when run, it should print:'
  say ''
  say '         packed: 11 22 33 44'
  say '         unpacked: 11223344'
  say ''
  say '     Using functions to access a full cell of a packed array.'
  say '-----'
  pawncc 'MIX_PACK_FUNC= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'packed: 11 22 33 44', 'unpacked: 11223344'
  return

test141:
  say '141. The following TWO test should compile successfully.'
  say ''
  say '     Clearing a string by assigning it an empty literal string.'
  say '     Assigning an empty literal string to a simple array must always succeed,'
  say '     because an array must always have at least one element.'
  say '-----'
  pawncc 'CLEAR_STRING1= test11' commandsuffix
  pawncc 'CLEAR_STRING2= test11' commandsuffix
  call matchoutput 'Completed successfully', 'Completed successfully'
  /* Symptoms of detected bug: error 046 (unknown array size).
   */
  return

test142:
  say '142. The following test should compile successfully.'
  say ''
  say '     A function taking no parameters with the compound block ending on the'
  say '     same line.'
  say '-----'
  pawncc 'CLOSING_BRACE_NO_PARMS= test2' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: error 001 (expecting ";" but finding "}").
   */
  return

test143:
  say '143. The following test should compile successfully.'
  say ''
  say '    Accessing an element in a symbolic pseudo-array declared as packed.'
  say '-----'
  pawncc 'PSEUDO_PACKED_ARRAY_PACKED= test11' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: warning 229 (mixing packed & unpacked).
   */
  return

test144:
  say '144. The compilation should issue warning 229.'
  say ''
  say '    Accessing an element in a symbolic pseudo-array declared as packed, but'
  say '    accessed as unpacked.'
  say '-----'
  pawncc '-v0 PSEUDO_PACKED_ARRAY_UNPACKED= test11' commandsuffix
  call matchoutput 'warning 229'
  /* Symptoms of detected bug: no warning, because the pseudo-array inherits the
     packed/unpacked flag of the main array.
   */
  return

test145:
  say '145. The following test should compile successfully; when run, it should print:'
  say ''
  say '         short1'
  say '         short2'
  say '         long message 1'
  say '         long message 2'
  say '-----'
  pawncc 'VARIABLE_3D_ARRAY= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'short1', 'short2', 'long message 1', 'long message 2'
  /* Symptoms of detected bug: the last line printed the wrong text, because the
     vector tables for the 3-dimensional array (with all variable dimensions) were
     optimized incorrectly.
   */
  return

test146:
  say '146. The following test should compile successfully; when run, it should print:'
  say ''
  say '         1test'
  say '         21test'
  say '         321test'
  say '         4321tes'
  say '         54321te'
  say '         654321t'
  say '-----'
  pawncc 'STRINS_MAXLENGTH= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', '1test', '21test', '321test', '4321tes', '54321te', '654321t'
  /* Symptoms of detected bug: native strins() ignored the maxlength parameter.
   */
  return

test147:
  say '147. The compilation should issue warning 208 (forward declaration recommended).'
  say ''
  say '    Conditional-compile for a section based on whether a symbol is defined,'
  say '    where that symbol is defined later in the code, forcing an extra pass.'
  say '-----'
  pawncc '-v0 test15' commandsuffix
  call matchoutput 'warning 208'
  /* Symptoms of detected bug: no warning, no extra pass, but bad code being
     generated.
   */
  return

test148:
  say '148. The following test should compile successfully; when run, it should print:'
  say ''
  say '         1 2 3'
  say '         4 5 6'
  say '         7 8 9'
  say '-----'
  pawncc 'INIT_ARRAY_TRAILING_COMMA= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', '1 2 3', '4 5 6', '7 8 9'
  /* Symptoms of detected bug: The list runs from 0 to 8 (instead of 1 to 9).
   */
  return

test149:
  say '149. The compilation should issue error 021 (symbol already defined).'
  say ''
  say '    A duplicate definition of a "goto" label.'
  say '-----'
  pawncc '-v0 DUPLICATE_LABEL= test2' commandsuffix
  call matchoutput 'error 021', '1 Error.'
  /* Symptoms of detected bug: no warning, first declaration being dropped.
   */
  return

test150:
  say '150. The compilation should issue warning 226 (variable assigned to itself),'
  say '     but ONLY ONCE and only for variable "b"'
  say '-----'
  pawncc '-v0 PRAGMA_WARNING= test1' commandsuffix
  call matchoutput 'warning 226*(symbol ''b'')', '1 Warning.', '/(symbol ''a'')'
  return

test151:
  say '151. The following test should compile successfully.'
  say ''
  say '     Using function that returns array in conditional operator before its'
  say '     definition is valid.'
  say '-----'
  pawncc 'BEFOREDEF= funretarrincondop' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: assertion failure in the assembler stage.
   */
  return

test152:
  say '152. The following test should compile successfully.'
  say ''
  say '     Using function that returns array in nested conditional operator is valid.'
  say '-----'
  pawncc 'NESTED= funretarrincondop' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: assertion failure in the assembler stage.
   */
  return

test153:
  say '153. The compilation should issue error 029 (invalid expression).'
  say ''
  say '     Invalid syntax on a return statement.'
  say '-----'
  pawncc '-v0 issue45' commandsuffix
  call matchoutput 'error 029', '1 Error.'
  /* Symptoms of detected bug: the invalid syntax caused the compiler to skip
     the closing "}" and thus fail to detect the end of the function, which caused
     the operator to be marked as "unused", which caused that the error was not
     reported.
   */
  return

test154:
  say '154. The compilation should issue warning 218 (deprecated forward declaration)'
  say '     followed by error 013 (no entry point).'
  say ''
  say '     Using a semicolon as the "empty statement".'
  say '-----'
  pawncc '-v0 EMPTY_MAIN= test14' commandsuffix
  call matchoutput 'warning 218', 'error 013', '1 Error.'
  /* Symptoms of detected bug: as the entry function is prototyped only, no code is
     generated for it; but due to the prototype, no warning is given either.
   */
  return

test155:
  say '155. The compilation should issue error 009 (invalid array size).'
  say ''
  say '     An array with major dimension of zero and valid minor dimension".'
  say '-----'
  pawncc '-v0 ZERO_ARRAY_TWO_DIMS= test1' commandsuffix
  call matchoutput 'error 009', '1 Error.', '/Assertion failed'
  /* Symptoms of detected bug: no error, but failed on an assertion later on.
   */
  return

test156:
  say '156. The following test should compile successfully.'
  say ''
  say '     Stock function that takes parameters with default values.'
  say '-----'
  pawncc 'STOCKFUNC_DEFPARAMS= test2' commandsuffix
  call matchoutput 'Completed successfully'
  return

test157:
  say '157. The compilation should issue warning 211.'
  say ''
  say '    Assignment inside the test condition of an if, for or while statement is'
  say '    possibly non-intentional.'
  say '-----'
  pawncc '-v0 -p ASSIGN_IN_TEST= test1' commandsuffix
  call matchoutput 'warning 211'
  return

test158:
  say '158. The following THREE tests should all compile successfully.'
  say ''
  say '    Optional parentheses in test expressions of if/while statements.'
  say '-----'
  pawncc '-p ALT_TEST1= test16' commandsuffix
  pawncc '-p ALT_TEST2= test16' commandsuffix
  pawncc '-p ALT_TEST3= test16' commandsuffix
  call matchoutput 'Completed successfully', 'Completed successfully', 'Completed successfully'
  return

test159:
  say '159. The compilation should issue error 028.'
  say ''
  say '    When test expression omit (optional) parentheses, packed array indexing must'
  say '    not be detected as opening brace that ends the expression.'
  say '-----'
  pawncc '-v0 -p ALT_TEST4= test16' commandsuffix
  call matchoutput 'error 028', '1 Error.'
  return

test160:
  say '160. The following test should compile successfully.'
  say ''
  say '    Optional parentheses in switch expression.'
  say '-----'
  pawncc '-p ALT_TEST5= test16' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: error 028 because opening brace is mistaken for a
     (packed) array index.
   */
  return

test161:
  say '161. The following test should compile successfully.'
  say ''
  say '    By exception, "while true" should NOT give a warning 206.'
  say '-----'
  pawncc 'WHILE_TRUE= test2' commandsuffix
  call matchoutput 'Completed successfully'
  return

test162:
  say '162. The following test should issue error 096.'
  say ''
  say '    Returning array literals (or string literals) is not supported.'
  say '-----'
  pawncc '-v0 RETURN_ARRAY_LITERAL= test2' commandsuffix
  call matchoutput 'error 096', '1 Error.', '/assertion failed'
  return

test163:
  say '163. The following test should compile successfully; when run, it should print:'
  say ''
  say '    OK'
  say '-----'
  pawncc 'STATE_VAR_INIT= states' commandsuffix
  pawnrun 'states.amx' commandsuffix
  call matchoutput 'Completed successfully', 'OK'
  /* Symptoms of detected bug: variables with states attached were not zero'ed out on
     entry from a state outside their list.
   */
  return

test164:
  say '164. The following test should compile successfully.'
  say ''
  say '-----'
  pawncc 'DO_WHILE_SEMICOLON= test1' commandsuffix
  call matchoutput 'Completed successfully'
  /* Symptoms of detected bug: error 036 (empty statement) due to a trailing
     semicolon.
   */
  return

test165:
  say '165. The following test should compile successfully; when run, it should print:'
  say ''
  say '    OK'
  say '-----'
  pawncc 'PARENTHESIZED_LVALUE= test2' commandsuffix
  pawnrun 'test2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'OK'
  /* Symptoms of detected bug: error 022 for an expression like "(abc) = 3", in which
     case the parenthesis caused the left operand become an rvalue.
   */
  return

test166:
  say '166. The following test should issue warning 218 (followed by fatal error 311),'
  say '     but the error must be located in test1.p and not in renamed_file.'
  say ''
  say '    The main file was renamed using #file, but the error declaration occurs'
  say '    above the rename (so the original name should still apply).'
  say '-----'
  pawncc '-v0 -p RESET_FILE_DIRECTIVE= test1' commandsuffix
  call matchoutput 'test1.p*warning 208', 'test1.p*fatal error 311'
  /* Symptoms of detected bug: the warning and error were flagged in the renamed
     file.
   */
  return

test167:
  say '167. The following test should issue error 001 (expecting a field name).'
  say ''
  say '    Declaration of an array with symbolic subscripts, but the symbolic name is'
  say '    invalid.'
  say '-----'
  pawncc '-v0 -p INVALID_ARRAY_SUBSCRIPT= test1' commandsuffix
  call matchoutput 'error 001*field/parameter', '1 Error.', '/assertion failed'
  /* Symptoms of detected bug: compiler dropped into an assertion.
   */
  return

test168:
  say '168. The following test should issue error 094.'
  say ''
  say '    Declaration of an array with symbolic subscripts, but which is then indexed'
  say '    with a literal constant.'
  say '-----'
  pawncc '-v0 -p REQUIRE_SYMBOLIC_SUBSCRIPT= test1' commandsuffix
  call matchoutput 'error 094', '1 Error.', '/assertion failed'
  /* Symptoms of detected bug: compiler dropped into an assertion.
   */
  return

test169:
  say '169. The following test should issue error 097, but it should locate it in'
  say '     test1.p and not in (none).'
  say ''
  say '    #pragma warning push without corresponding pop.'
  say '-----'
  pawncc '-v0 -p UNBALANCED_PRAGMA= test1' commandsuffix
  call matchoutput 'test1.p*error 097', '1 Error.'
  /* Symptoms of detected bug: filename reset to (none) because filename was
     free'd before the check was made.
   */
  return

test170:
  say '170. The following test should compile successfully (and not drop into an'
  say '     assertion).'
  say ''
  say '    Test for the peephole optimizer when using 64-bit cells.'
  say '-----'
  pawncc '-O3 -C64 LINE_CAT= test2' commandsuffix
  call matchoutput 'Completed successfully', '/assertion failed'
  /* Symptoms of detected bug: compiler assertion due to the replacement string
     being longer than the matched string, which is due to literal numbers being
     expanded to cell size.
   */
  return

test171:
  say '171. The following test should issue error 001 (expecting ''='').'
  say ''
  say '    Declaration of a global variable but with a string literal following it.'
  say '-----'
  pawncc '-v0 -p LOOSE_STRING= test1' commandsuffix
  call matchoutput 'error 001: expected ''=''', '1 Error.', '/Assertion failed'
  /* Symptoms of detected bug: compiler dropped into an assertion, due to stray
     data in the literal pool.
   */
  return

test172:
  say '172. The following test should issue error 085.'
  say ''
  say '    Declaration of a function for the fallback state, but no other states are'
  say '    defined.'
  say '-----'
  pawncc '-v0 -p -r FALLBACK_STATE_ONLY= test1' commandsuffix
  call matchoutput 'error 085', '1 Error.', '/Assertion failed'
  /* Symptoms of detected bug: compiler dropped into an assertion after flagging
     the error.
   */
  return

test173:
  say '173. The following test should issue error 001 followed by error 052 (and then'
  say '     others).'
  say ''
  say '    A multi-dimensional array with an incomplete initialization.'
  say '-----'
  pawncc '-v0 -p ARRAY_INCOMPLETE_INITIALLER= test1' commandsuffix
  call matchoutput 'error 001', 'error 052', '3 Errors.', '/Assertion failed', '/Fortify: Memory List'
  /* Symptoms of detected bug: compiler dropped into an assertion (before flagging
     the error).
   */
  return

test174:
  say '174. The following test should compile successfully (and not drop into an'
  say '     assertion).'
  say ''
  say '    User-defined operator calling a subfunction.'
  say '-----'
  pawncc '-p USER_OPERATOR_REF= test1' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Symptoms of detected bug: compiler assertion due to the generated function
     name being adjusted (replaced by a new symbol), but the list of references
     (for the report) not being copied to the new symbol.
   */
  return

test175:
  say '175. The following test should issue error 030.'
  say ''
  say '    Compound statement not closed, but no newline after the ''{'' either.'
  say '-----'
  pawncc '-v0 -p minimal' commandsuffix
  call matchoutput 'error 030', '1 Error.', '/Assertion failed'
  /* Symptoms of detected bug: compiler dropped into an assertion.
   */
  return

test176:
  say '176. The following FOUR tests should each compile successfully.'
  say ''
  say '    Tests with ''#pragma unused''.'
  say '-----'
  pawncc '-p PRAGMA_UNUSED_1= test17' commandsuffix
  pawncc '-p PRAGMA_UNUSED_2= test17' commandsuffix
  pawncc '-p PRAGMA_UNUSED_3= test17' commandsuffix
  pawncc '-p PRAGMA_UNUSED_4= test17' commandsuffix
  call matchoutput 'Completed successfully', 'Completed successfully', 'Completed successfully', 'Completed successfully'
  /* Symptoms of detected bug: when the '#pragma unused' followed a declaration
     of a (local) variable immediately, the variable was not yet defined when
     the directive was parsed (preprocessing interferes with parsing).
   */
  return

test177:
  say '177. The following test should issue warning 214.'
  say ''
  say '    Array parameter that is non-const and that has a default value.'
  say '-----'
  pawncc '-v0 -p NON_CONST_ARRAY_W_DEFAULT= test1' commandsuffix
  call matchoutput 'warning 214'
  return

test178:
  say '178. The following test should issue warning 206 TWICE, but not error 021.'
  say ''
  say '    Same variable diclared in two conditional blocks.'
  say '-----'
  pawncc '-v0 -p siblings' commandsuffix
  call matchoutput 'warning 206', 'warning 206', '2 Warnings.', '/error 021', '/Assertion failed'
  /* Sympton of detected bug: Due to incorrect testing of the block (nesting) level
     of variables, two variables with the same name but in different "sibling"
	 blocks caused the error 021 (symbol already defined) to be flagged for the
	 second declaration.
   */
  return

test179:
  say '179. The following test should issue warning 206. When running, it shows:'
  say ''
  say '    destruct(0): 8'
  say ''
  say '    Destructor on local variable in a nested block.'
  say '-----'
  pawncc '-v0 dtor' commandsuffix
  pawnrun 'dtor.amx' commandsuffix
  call matchoutput 'warning 206', '1 Warning.', 'destruct(0): 8', '/Assertion failed'
  /* Sympton of detected bug: 1) The compiler dropped into an assertion. 2) The
     generated code did not call the destructor.
   */
  return

test180:
  say '180. The following test should issue error 001 followed by other errors.'
  say ''
  say '    Syntax error in the ''state'' statement.'
  say '-----'
  pawncc '-v0 -p STATE_INCOMPLETE= states' commandsuffix
  call matchoutput 'error 001', 'error 030', '2 Errors.', '/Assertion failed'
  /* Sympton of detected bug: The compiler dropped into an assertion.
   */
  return

test181:
  say '181. The following tests should issue error 017 followed by waring 203, both for'
  say '     the same symbol ''g''.'
  say ''
  say '    Another test with ''#pragma unused''.'
  say '-----'
  pawncc '-v0 -p PRAGMA_UNUSED_5= test17' commandsuffix
  call matchoutput 'error 017*''g''', 'warning 203*''g''', '1 Error.'
  /* Sympton of detected bug: The compiler crashed.
   */
  return

test182:
  say '182. The following tests should issue error 001 TWICE.'
  say ''
  say '    Literal array passes to a function, but with an incorrect syntax.'
  say '-----'
  pawncc '-v0 -p LITERAL_ARRAY_PARAM= array2' commandsuffix
  call matchoutput 'error 001', 'error 001', '2 Errors.'
  /* Sympton of detected bug: Assertion failure due to the syntax error in the
     literal array, which then causes the array size to be determined as 0.
   */
  return

test183:
  say '183. The following tests should issue error 001.'
  say ''
  say '    Literal array is assigened to a variable, but with an incorrect syntax.'
  say '-----'
  pawncc '-v0 -p LITERAL_ARRAY_ASSIGN= array2' commandsuffix
  call matchoutput 'error 001', '1 Error.'
  /* Sympton of detected bug: Assertion failure due to the syntax error in the
     literal array, which then causes the array size to be determined as 0 (same
	   as in test 182). A second problem was an extra error 047 being issued.
   */
  return

test184:
  say '184. The following tests should compile successfully and show verbose output.'
  say '     On disassembly, the debug information must show at the bottom:'
  say ''
  say '    Symbols Address         Scope                   Name'
  say '            FRM(0000000c)   00000008:000000ac       n'
  say '            FRM(fffffffc)   00000108:000001c8       v'
  say '            00000008        00000008:000000ac       fibonacci'
  say '            000000ac        000000ac:000001d4       main'
  say ''
  say '    Scope (code range) of function arguments must cover the entire function.'
  say '-----'
  pawncc '-d2 fibr' commandsuffix
  pawndisasm '-d -o- -r4:0 fibr.amx' commandsuffix
  call matchoutput 'Completed successfully', 'DEBUG INFORMATION','FRM(0000000c)*00000008:000000ac*n'
  /* Sympton of detected bug: code range of function argument was reduced to the
     compound block of the function (in the debug information only). That is,
     the scope of function argument 'n' must be the same as function 'fibonacci'.
   */
  return

test185:
  say '185. The following tests should issue warning 220.'
  say ''
  say '    Local variable declared between ''goto'' and its label.'
  say '-----'
  pawncc '-v0 -p GOTO_SKIPS_DECL= test1' commandsuffix
  call matchoutput 'warning 239', '1 Warning.'
  return

test186:
  say '186. The following tests should issue warning 214 TWICE. The disassembly should'
  say '     show:'
  say ''
  say '    000000d8        proc            ; main'
  say '    000000dc        break           ; line 16'
  say '    000000e0        break           ; line 18'
  say '    000000e4        push.c 00000020'
  say '    000000ec        push.c 00000004'
  say '    000000f4        call 00000008   ; func1'
  say '    000000fc        break           ; line 19'
  say '    00000100        push.c 00000024'
  say '    00000108        push.c 00000004'
  say '    00000110        call 00000070   ; func2'
  say '    00000118        zero.pri'
  say '    0000011c        retn'
  say ''
  say '    Passing identical literal strings to functions whose parameters are'
  say '    non-const. The compiler warns that ''const'' allows to create better code,'
  say '    but the literal strings are not merged.'
  say '-----'
  pawncc '-v0 -O2 -d2 SUGGEST_CONST= test18' commandsuffix
  pawndisasm '-o- -r00d8:011c test18.amx' commandsuffix
  call matchoutput 'warning 214', 'warning 214', '2 Warnings.', 'push.c 00000020', 'call 00000008', 'push.c 00000024', 'call 00000070'
  return

test187:
  say '187. The following tests should compile successfully. The disassembly should'
  say '     show:'
  say ''
  say '    000000d8        proc            ; main'
  say '    000000dc        break           ; line 16'
  say '    000000e0        break           ; line 18'
  say '    000000e4        push.c 00000020'
  say '    000000ec        push.c 00000004'
  say '    000000f4        call 00000008   ; func1'
  say '    000000fc        break           ; line 19'
  say '    00000100        push.c 00000024'
  say '    00000108        push.c 00000004'
  say '    00000110        call 00000070   ; func2'
  say '    00000118        zero.pri'
  say '    0000011c        retn'
  say ''
  say '    Passing identical literal strings to functions whose parameters are const'
  say '    causes the literal strings to be merged,'
  say '-----'
  pawncc '-O2 -d2 LITERAL_MERGE_ARG= test18' commandsuffix
  pawndisasm '-o- -r00d8:011c test18.amx' commandsuffix
  call matchoutput 'Completed successfully', 'push.c 00000020', 'call 00000008', 'push.c 00000020', 'call 00000070'
  return

test188:
  say '188. The following tests should compile successfully. The disassembly should'
  say '     show:'
  say ''
  say '    00000008        proc            ; main'
  say '    0000000c        break           ; line 41'
  say '    00000010        break           ; line 43'
  say '    00000014        stack fffffff8'
  say '    0000001c        zero.pri'
  say '    00000020        addr.alt fffffff8'
  say '    00000028        movs 00000008'
  say '    00000030        break           ; line 44'
  say '    00000034        addr.alt fffffff8'
  say '    0000003c        zero.pri'
  say '    00000040        movs 00000008'
  say '    00000048        stack 00000008'
  say '    00000050        zero.pri'
  say '    00000054        retn'
  say ''
  say '    Assigning identical literal strings to arrays; these strings should be'
  say '    merged.'
  say '-----'
  pawncc '-O2 -d2 LITERAL_MERGE_ASSIGN= test18' commandsuffix
  pawndisasm '-o- -r0008:0054 test18.amx' commandsuffix
  call matchoutput 'Completed successfully', 'zero.pri', 'addr.alt fffffff8', 'movs 00000008', ,
                   'addr.alt fffffff8', 'zero.pri', 'movs 00000008'
  return

test189:
  say '189. The following tests should compile successfully.'
  say ''
  say '    A function returning a literal array, called before its definition.'
  say '-----'
  pawncc 'FORWARD_FUNC_ARRAY= array2' commandsuffix
  call matchoutput 'Completed successfully', '/Assertion failed'
  /* Sympton of detected bug: assertion failed on the array size; the return of
     the undefined function was incorrectly considered to be a literal array
   */
  return

test190:
  say '190. The following tests should compile successfully. When it runs, it should'
  say '    print:'
  say ''
  say '     b00=5 b10=6'
  say ''
  say '    The indirection vectors of a multi-dimensional array are merged as literal'
  say '    data, if identical to an earlier array declaration.'
  say '-----'
  pawncc 'MERGED_ARRAY_VECTOR_INIT= array2' commandsuffix
  pawnrun 'array2.amx' commandsuffix
  call matchoutput 'Completed successfully', 'b00=5 b10=6'
  /* Sympton of detected bug: duplicate data (merged) was detected, but copied
     incorrectly.
   */
  return

test191:
  say '191. The following tests should issue warning 206 (redundant test).'
  say ''
  say '    ''#pragma undefined'' on a symbol declared in a lower block.'
  say '-----'
  pawncc '-v0 PRAGMA_UNUSED_BLOCKLVL= test17' commandsuffix
  call matchoutput 'warning 206', '1 Warning.'
  /* Sympton of detected bug: the block level was set the that of the #pragma,
     instead of that of the symbol.
   */
  return

test192:
  say '192. The following tests should compile successfully. When it runs, it should'
  say '    print:'
  say ''
  say '    count = 3'
  say '    sum = 8'
  say '    fields = 0 1 2'
  say ''
  say '    Tests of the intrinsic variables ''argc'' and ''argv''.'
  say '-----'
  pawncc 'sum' commandsuffix
  pawnrun 'sum.amx' commandsuffix
  call matchoutput 'Completed successfully', 'count = 3', 'sum = 8', 'fields = 0 1 2'
  return

test193:
  say '193. The following tests should issue error 101 (numeric overflow).'
  say ''
  say '    Constant expression whose result does not fit in a cell.'
  say '-----'
  pawncc '-v0 NUMERIC_OVERFLOW= test1' commandsuffix
  call matchoutput 'error 101', '1 Error.'
  return

test194:
  say '194. The following tests should issue warning 242 (numeric overflow) TWICE, with'
  say '     warning 234 in between.'
  say ''
  say '    ''#pragma deprecated'' not followed by a declaration.'
  say '-----'
  pawncc '-v0 deprecated' commandsuffix
  call matchoutput 'warning 242', 'warning 234', 'warning 242', '3 Warnings.'
  return

test195:
  say '195. The following tests should issue warning 203 (symbol ''l'' never used).'
  say ''
  say '    Function body contains only a label (no compound statement).'
  say '-----'
  pawncc '-v0 EMPTY_MAIN2= test14' commandsuffix
  call matchoutput 'warning 203*symbol ''l''', '1 Warning.'
  return

test196:
  say '196. The following tests should issue error 029 followed by error 054.'
  say ''
  say '    Expression of an ''if'' statement starts with a ''('' but lacks the closing '')''.'
  say '-----'
  pawncc '-v0 MISSING_OPTIONAL_PARENTHESIS= test14' commandsuffix
  call matchoutput 'error 029', 'error 054', '2 Errors.'
  return

test197:
  say '197. The following tests should issue warning 243 (number does not fit in a'
  say '     cell) TWICE, for 4294967296 and 0x1''0000''0000.'
  say ''
  say '    Numbers that do not fit in a 32-bit cell, next to numbers that do (the'
  say '    bit pattern of -1, and cellmin).'
  say '-----'
  pawncc '-v0 -C32 NUMBER_TOO_LARGE= test14' commandsuffix
  call matchoutput 'warning 243*4294967296', 'warning 243*0x1''0000''0000', '2 Warnings.', '/0xffffffff', '/2147483648'
  return


