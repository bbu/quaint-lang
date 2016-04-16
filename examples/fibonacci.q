entry
{
    /*
     * Our recursive fibonacci function has exponential complexity and is very
     * computationally-intensive. A synchronous call to fibonacci(32:u32) takes
     * about 6 seconds on my i5 MacBook Air:
     */
    ps("Synchronous call: "), pu32(fibonacci(32 as u32)), pnl();

    /* Now, call the same function asynchronously, in steps of 1 second */
    fibq: quaint(u32) = ~fibonacci(32 as u32);

    ps("At start: "), pu8(fibq@start), pnl();
    iter: u32 = 0:u32;

    do {
        wait fibq for 1000 msec;
        ps("Iteration "), pu32(iter++), pnl();
    } while !fibq@end;

    ps("At end: "), pu8(fibq@end), pnl();
    const value: u32 = *fibq;
    ps("Reaped value: "), pu32(value), pnl();
}

fibonacci(number: u32): u32
{
    if number == 0:u32 || number == 1:u32 {
        return number;
    } else {
        return fibonacci(number - 1:u32) + fibonacci(number - 2:u32);
    }
}
