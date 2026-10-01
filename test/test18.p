#if defined SUGGEST_CONST
func1(msg[])
    {
    print msg
    print " - from func1"
    }

func2(str[])
    {
    print str
    print " - from func2"
    }

main()
    {
    func1 "abc"
    func2 "abc"
    }
#endif

#if defined LITERAL_MERGE_ARG
func1(const msg[])
    {
    print msg
    print " - from func1"
    }

func2(const str[])
    {
    print str
    print " - from func2"
    }

main()
    {
    func1 "abc"
    func2 "abc"
    }
#endif

#if defined LITERAL_MERGE_ASSIGN
main()
    {
    var array{6} = "abcde"
    array = "abcde"
    }
#endif

