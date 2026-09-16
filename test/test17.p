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


