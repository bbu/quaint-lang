type mytype: struct(
    a: ptr[5](vptr),
    b: byte,
    c: quaint(u32)
);

type myint: u64;

entry
{
    /* pointer without a subtype */
    p: ptr();
    p: ptr;

    /* void pointer with subtype */
    p: vptr(int);

    /* quaint without a subtype */
    q: quaint;

    /* OK, quaint with void subtype */
    q: quaint();

    /* empty struct */
    s: struct();

    /* non-existent type */
    n: some_unknown_type;

    /* OK, array of size zero */
    arr: u64[0];

    /* OK */
    s: mytype = 0 as mytype;
    n: myint;

    /* primitive type with a subtype */
    arr: int[5](vptr);

    /* array of pointers without a subtype */
    arr: ptr[5];

    /* bad function pointers */
    fp: fptr(/* name:type pairs expected */ 5 + 3, 4): int;
    fp: fptr(/* no types/names */ x, y): int;
    fp: fptr(/* duplicate names */ a: int, a: byte): int;
    fp: fptr /* must at least have () */;

    /* duplicate member name */
    s: struct(
        /* p is an array(8) of pointers to pointer to byte */
        p: ptr[8](ptr(byte)),

        /* f points to a function taking x and y, returning array(5) of int */
        f: fptr(x: byte, y: int): int[5],

        /* union with duplicate members  */
        u: union(dup: byte, b: int, dup: ptr(vptr), d: uptr),

        /* member p already exists */
        p: int
    );

    /* duplicate name in declaration list */
    same, b, same: int = 5 as int;
}
