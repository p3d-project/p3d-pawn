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
