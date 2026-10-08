sum(...)
    {
    printf "count = %d\n", argc
    var result = 0
    for var i = 0; i < argc; ++i
        {
        result += argv[i]
        argv[i] = i
        }
    return result
    }

main()
    {
    var a = 1
    var b = 3
    var c = 4
    var x = sum(a, b, c)
    printf "sum = %d\n", x
    printf "fields = %d %d %d\n", a, b, c
    }

