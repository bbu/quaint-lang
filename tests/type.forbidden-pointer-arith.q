entry
{
    vp: vptr = null;

    /* arithmetic on void pointer */
    vp++, ++vp, vp--, --vp;
    vp += 2:usize;
    vp -= 2:usize;
    vp + 3:usize;

    /* arithmetic on function pointer */
    fp: fptr(a: int): int = null as fptr(a: int): int;
    fp++, ++fp, fp--, --fp;
    fp += 2:usize;
    fp -= 2:usize;
    fp + 3:usize;

    /* dereferencing a vptr or a fptr */
    *vp;
    *fp;

    /* OK */
    *(vp as ptr(int));
}
