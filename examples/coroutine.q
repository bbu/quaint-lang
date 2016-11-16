type status: enum(exit, continue, cancel): byte;

entry
{
    stat: status = status::continue;
    q: quaint(ptr(byte)) = ~infinite_function(&stat);
    i: byte = 0;

    do {
        wait q until infinite_function::before_print;
        ps("In caller"), pnl();
        wait q until infinite_function::after_print;

        if ++i == 10 {
            stat = status::exit;
            wait q;
        }
    } while !q@end;

    ps("Function returned: "), ps(*q), pnl();

    {
        stat = status::cancel;
        q = ~infinite_function(&stat);
        wait q;
        ps("Function returned: "), ps(*q), pnl();
    }
}

infinite_function(arg: ptr(status)): ptr(byte)
{
    while true {
        [before_print]
        ps("In callee"), pnl();
        [after_print]

        if *arg == status::exit {
            return "ended with exit";
        } elif *arg == status::cancel {
            return "ended with cancel";
        }
    }
}
