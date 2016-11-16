a, b, temp: byte;

entry
{
    a = 5, b = 3;
    q: quaint() = ~func();

    do {
        wait q for 1 msec;
        ps("OUTER iteration"), pnl();
    } while !q@end;

    *q;
}

func
{
    i: u64 = 0:u64;

    while ++i < 100:u64 {
        noint {
            ps("Before swap: "), pu8(a), ps(", "), pu8(b), pnl();
            temp = a;
            a = b;
            b = temp;
            ps("After swap: "), pu8(a), ps(", "), pu8(b), pnl();
        }
    }
}
