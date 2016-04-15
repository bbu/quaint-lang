entry
{
    q: quaint(u64) = ~pointless_function();

    wait q until pointless_function::label_a;
    ps("At label_a: "), pu8(q@pointless_function::label_a), pnl();

    wait q until pointless_function::label_b;
    ps("At label_b: "), pu8(q@pointless_function::label_b), pnl();

    wait q until pointless_function::label_c;
    ps("At label_c: "), pu8(q@pointless_function::label_c), pnl();

    wait q;
    ps("At end: "), pu8(q@end), pnl();

    ps("Result: "), pu64(*q), pnl();
}

pointless_function: u64
{
    i: u64 = 0 as u64;

    while ++i < 1000000:u64 {
        x: vptr = malloc(1024:usize);
        free(x);

        if i == 300000:u64 {
            [label_a]
        } elif i == 600000:u64 {
            [label_b]
        } elif i == 900000:u64 {
            [label_c]
        }
    }

    return i;
}
