#if defined PRAGMA_UNUSED_1

main() {
    new local
    #pragma unused local
    return 0;
}

#endif

#if defined PRAGMA_UNUSED_2

main() {
    new local
    return 0;
    #pragma unused local
}

#endif

#if defined PRAGMA_UNUSED_3

func(abc) {
    #pragma unused abc
    return 0
}

main() {
    new local = func(1)
    local = -local
    return 0;
}

#endif

#if defined PRAGMA_UNUSED_4
/* #pragma at a deeper block level than the variable */
main()
    {
    new y;
    if (y == 0)
        {
        #pragma unused y
        }
    }
#endif

#if defined PRAGMA_UNUSED_5
/* unused global, but #pragma is local */
new g;

main()
    {
    #pragma unused g
    }
#endif

#if defined PRAGMA_UNUSED_BLOCKLVL

main()
    {
    new y;
    if (1)
        {
        #pragma unused y
        return;
        }
    }

#endif
