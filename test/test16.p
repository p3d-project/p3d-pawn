
main()
    {
    #if defined ALT_TEST1 g
        var abc = 1
        if abc == 1 {
            abc++
        }
        var def = 2
        if abc == def {
            abc++
            def--
        }
    #endif

    #if defined ALT_TEST2
        var abc{10}
        if abc{0} == 1 {
            abc{1} = 2
        }
        var def =2
        if def == abc{0} {
            abc{1} = def
        }
    #endif

    #if defined ALT_TEST3
        var abc
        if abc == 1
            abc++
        var def
        if abc == def
            abc = def + 1
    #endif

    #if defined ALT_TEST4
        var abc = 1
        if 1 == abc{0} {
            abc+=1
        }
    #endif

    #if defined ALT_TEST5
        var abc = 1
        switch abc
            {
            case 0: abc++
            case 1: abc--
            }
    #endif
    }
