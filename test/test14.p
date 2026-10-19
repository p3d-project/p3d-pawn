#if defined INTERNAL_PUBLIC_CALL
public plugin_init()
{
   cmd(1)
}

public cmd(num)
{
}
#endif

#if defined EMPTY_MAIN
main()
    ;
#endif

#if defined EMPTY_MAIN2
main()
    l:
#endif

#if defined MISSING_OPTIONAL_PARENTHESIS
main()
if(
}
#endif

#if defined NUMBER_TOO_LARGE
main()
{
    new a = 4294967296;         /* 2^32 */
    new b = 0x1'0000'0000;      /* 2^32 */
    new c = 0xffffffff;         /* the bit pattern of -1 in a 32-bit cell */
    new d = -2147483648;        /* cellmin of a 32-bit cell */
    #pragma unused a, b, c, d
}
#endif
