entry
{
    /* 255 is of type byte (u8), 256 is of type ushort (u16) */
    255 == 256;

    /* OK */
    255:u16 == 256;
    255 as u16 == 256;

    /* 5 is of type byte (u8), not int (i32) */
    a: int = 5;

    /* OK */
    b: int = 5:int;
    c: int = 5 as int;

    /* the ternary operator requires the same type and signedness */
    true ? 255 : 256;

    /* byte (u8) unsigned : sbyte (i8) signed */
    true ? 5 : -5;

    /* OK */
    true ? (255:u16) : 256;
    true ? (5:sbyte) : -5;

    /* && returns byte (u8) */
    d: int = true && false;

    /* OK */
    e: int = (true && false) as int;

    p: ptr(u64) = null as ptr(u64);

    /* pointer arithmetic requires a second operand of type usize */
    p + 3;

    /* OK */
    p + 3:usize;

    /* all bitwise operations require same-sized unsigned integral types */
    64 << 2:sbyte;
    64 & 32:int;
    64 ^ 32:int;
    ^5:int;

    /* OK */
    64 << 2;
    64 & 32;
    64 ^ 32;
    ^5;
}
