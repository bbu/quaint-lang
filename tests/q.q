use "";
use "libs/trig/math" as math;
use "libs/abc/math" as default;

type a: struct(a: int, b: byte, c: long, d: u8);
exposed type b: union(c: int);

exposed type e1: enum(red, green, blue): u32;
exposed type e2: enum(abc, def, dd): u32;
exposed type e3: e2[3];
// math::e1::green, e1::green

exposed xxx: ptr(int[15]) = null as ptr(int[15]);

exposed entry
{
    pu32(fibonacci(32:u32)), pnl();
    fib1: quaint(u32) = ~fibonacci(32 as u32);
    fib2: quaint(u32) = ~fibonacci(30 as u32);
    iter: u32 = 0:u32;
    aa, bb: e1;
    aa = bb;

    [%glab]
    const pipe_id: int = 3; [@wlab]

    send item, sizeof(t);

    //send data, sizeof(data) to

    //spawn 5;

    if id == 1 {
        q <- 4;
    } elif id == 2 {
        q -> i;
    } else {

    }

    for idx_i(0, 4): int {
        for idx_j(0, 2): long {
            test(idx_i, idx_j);
        }
    }

    match(expr, case_expr, val_expr, case_expr, val_expr);
    match(expr, 3, foo, 5, bar);

    match ball.color {
    case red, green, blue, yellow {
        test();
        test();
    }

    case black, silver, white:

    }
    }

    spawn 3;

    x: e1 = e1::blue + e2::abc;
    ps("x: "), pu32(x:u32), pnl();
    //ps("eq: "), pu8(x == e2::aa), pnl();
    ps("Test: "), pu8(123), pnl();

    do {
        wait fib1 for 1000 msec;
        //wait fib2 for 500 msec;
        pu32(iter++:u32), pnl();
        ps("Another string "), ps("yet another"), pnl();
    } while !fib1@end;

    pu32(*fib1), pnl(), pu32(*fib2), pnl();
}

test
{
    q: queue(struct(a: int, b: int)) = mq(8);

    for idx(start: 0, stop: 4, step: 1): int {

    }

    q -> 3;
    q <- 3;
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
