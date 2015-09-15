type mytype: struct(a: int, b: byte, c: byte[3]);

entry
{
    /* non-lvalue subexpressions */
    5 = 3;
    5 += 3;
    5 >>= 3;
    ((5 + 4) && false) = 3;
    entry() = null:fptr();
    !0 = 1;
    6++, ++7, 8--, --9;
    null++ && true-- && &false;
    &10;
    (false ? 1 : 2) = 3;

    /* attempting to modify a const-qualified variable */
    const c: byte[3] = 0 as byte[3];
    c[1]++, ++c[1], c[1]--, --c[1];
    c[2] = 5;

    /* attempting to modify a builtin fptr */
    exit = null as fptr(s: u32): u32;

    /* OK, a dereferenced pointer is an lvalue */
    *(null as ptr(int) + 42:usize) = 5:int;

    /* the value of a quaint is not an lvalue, though */
    *(null as quaint(int)) = 5:int;

    /*
       The cast is not an lvalue and the * requires an lvalue
       because it needs to free and nullify the quaint itself.
    */
    a: int = *(null as quaint(int));

    s: mytype = 0 as mytype;
    ps: ptr(mytype) = &s;

    /* OK */
    s.a = 3:int;
    s.b = 3;
    s.c[1] = 3;

    /* OK */
    ps->a = 3:int;
    ps->b = 3;
    ps->c[1] = 3;

    const p: ptr(ptr(ptr(byte))) = null as ptr(ptr(ptr(byte)));

    /* OK */
    ***p = 3;
    **p = null:ptr(byte);
    *p = null:ptr(ptr(byte));

    /* the p pointer itself is constant */
    p = null as ptr(ptr(ptr(byte)));
}
