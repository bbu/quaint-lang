type a: struct(a: int);
exposed type b: union(c: enum(red, green, blue): int);

exposed type e1: enum(red, green, blue): u64;
exposed type e2: enum(red, green, blue);
// math::e1::green, e1::green

entry
{
    pu32(fibonacci(32:u32)), pnl();
    fib1: quaint(u32) = ~fibonacci(32 as u32);
    fib2: quaint(u32) = ~fibonacci(30 as u32);
    iter: u32 = 0:u32;

    ps("Test: "), pu8(123), pnl();

    do {
        wait fib1 for 1000 msec;
        //wait fib2 for 500 msec;
        pu32(iter++:u32), pnl();
        ps("Another string "), ps("yet another"), pnl();
    } while !fib1@end;

    pu32(*fib1), pnl(), pu32(*fib2), pnl();
}

factorial(number: byte): ulong
{
    if number == 1 {
        return 1:ulong;
    }

    return factorial(number - 1) * number:ulong;
}

/* very slow, exponential fibonacci */
fibonacci(number: u32): u32
{
    if number == 0:u32 || number == 1:u32 {
        return number;
    } else {
        return fibonacci(number - 1:u32) + fibonacci(number - 2:u32);
    }
}

/* mutual recursion */
gcd_ternary(m: u64, n: u64): u64
{
    return m == n ? m : (m > n ? gcd_elif(m - n, n) : gcd_ternary(m, n - m));
}

gcd_elif(m: u64, n: u64): u64
{
    if m == n {
        return m;
    } elif m > n {
        return gcd_ternary(m - n, n);
    } else {
        return gcd_elif(m, n - m);
    }
}
