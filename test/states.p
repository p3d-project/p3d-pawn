
#if defined NAMED_AUTOMATON
forward @keypressed(key);

main()
    state foo:plain

@keypressed(key) <foo:plain>
    {
    state (key == '/') foo:plain
    printf ''%c'', key
    }
#endif

#if defined STATE_VAR_INIT
var A <x, y>
var B <x, z>
var C <p>
var error

show(i, j) <x>
    {
    if (A != i || B != j)
        {
        print "Fail <x>\n"
        error++
        }
    A += 1
    B += 1
    }

show(i, j) <y>
    {
    #pragma unused j
    if (A != i)
        {
        print "Fail <y>\n"
        error++
        }
    A += 1
    }

show(i, j) <z>
    {
    #pragma unused j
    if (B != i)
        {
        print "Fail <z>\n"
        error++
        }
    B += 1
    }

show(i, j) <p>
    {
    #pragma unused j
    if (C != i)
        {
        print "Fail <p>\n"
        error++
        }
    C += 1
    }

main()
    {
    state x
    show 0, 0   // A = 0, B = 0
    show 1, 1   // A = 1, B = 1
    state y
    show 2, 0   // A = 2
    state x
    show 3, 0   // A = 3, B = 0
    state z
    show 1, 0   // B = 1
    state x
    show 0, 2   // A = 0, B = 2
    state p
    show 0, 0   // C = 0
    state x
    show 0, 0   // A = 0, B = 0
    if (error==0)
        print "OK\n"
    }
#endif

#if defined STATE_INCOMPLETE
var key
main()
    {
    state (key
#endif
