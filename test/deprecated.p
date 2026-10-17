#pragma deprecated
#pragma deprecated just use printf

printval(a)
    {
    printf "fields = %d\n", a
    }

main()
    {
    var b = 12
    var a = b >> 0
    printval a
    }

#pragma deprecated abc

