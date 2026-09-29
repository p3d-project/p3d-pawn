/* dtor.p */

operator~(Res:r[], count)
    {
    for (new i = 0; i < count; i++)
        printf("destruct(%d): %d\n", i, _:r[i]);
    }

main()
    {
    if (1)
        {
        new Res:h = Res:7;
        const k = 1;
        h = Res:(_:h + k);
        }
    }
