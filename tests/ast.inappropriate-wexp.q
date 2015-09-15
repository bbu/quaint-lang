entry: int
{
    /* sec/msec may never appear out of the top-level context of a wait-for */
    3 + 4 * 2 msec;
    a: byte = 5sec;

    /* OK */
    q: quaint(u32) = null as quaint(u32);
    wait q for (3 + 4 * 2) sec;
    wait q for 150msec;
    result: u32 = *q;

    return 0:int;
}
