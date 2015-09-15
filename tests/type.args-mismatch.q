entry
{
    f1();
    f1(5, 6, 7);
    f1(5:int, 6, 7:u64, 1337);

    f2(3);
    f2(256, 256);

    f3(1);
    f3(1, 2);

    f4(1, 2);
    f4(1);

    /* void type is not convertible */
    f4() as int;

    /* type mismatch: byte + void */
    3 + f4();

    /* fptr does not match args */
    fp: fptr(a: int): u16 = f2;

    /* fptr does not match return type */
    fp: fptr(a: byte, b: byte) = f2;
    fp: fptr(a: byte, b: byte): u64 = f2;

    /* OK */
    const fp1: fptr(a: int, b: byte, c: u64): uptr = f1;
    f1(5:int, 6, 7:u64), fp1(5:int, 6, 7:u64);

    const fp2: fptr(a: byte, b: byte): u16 = f2;
    f2(255, 255), fp2(255, 255);

    const fp3: fptr(): byte = f3;
    f3(), fp3();

    const fp4: fptr() = f4;
    f4(), fp4();
}

f1(a: int, b: byte, c: u64): uptr
{
    return (a:u64 + b:u64 + c:u64) as uptr;
}

f2(a: byte, b: byte): u16
{
    return a:u16 + b:u16;
}

f3: byte
{
    return 255;
}

f4
{
    return;
}
