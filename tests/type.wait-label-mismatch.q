entry
{
    q: quaint() = ~f1();

    wait q until f1::non_existent_label;
    wait q until non_existent_func::label_a;
    wait q until f1;
    wait q until non_existent_func;
    q@somewhere;
    q@f1::non_existent_label;
    q@non_existent_func::label_e;

    /* OK */
    q@start;
    q@f1::label_a;
    q@f2::label_d;
    q@f3::label_g;
    q@end;
    wait q until f1::label_a;
    wait q until f1::label_b;
    wait q until f1::label_c;
    wait q until f2::label_d;
    wait q until f2::label_e;
    wait q until f2::label_f;
    wait q until f3::label_g;
    wait q until f3::label_h;
    wait q until f3::label_i;
    *q;
}

f1
{
    [label_a]
    [label_b]
    [label_c]
}

f2
{
    [label_d]
    [label_e]
    [label_f]
}

f3
{
    [label_g]
    [label_h]
    [label_i]
}
