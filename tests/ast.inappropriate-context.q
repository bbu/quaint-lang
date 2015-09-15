/* OK, declaration in unit context */
a: byte;

/* OK, type declaration in unit context */
type myint: long;

/* return in unit context */
return 3 + 4 * 2;

/* expression in unit context */
a = 5 + 4;

/* wait label in unit context */
[my_label]

/* wait statement in unit context */
wait q for 5 msec;

/* control-flow statement in unit context */
while true || false {
    1 + 1;
}

if 0 {
} elif 0 {
} elif 0 {
} else {
}

/* noint block in unit context */
noint {
    1 + 2;
}

/* lexical block in unit context */
{
    3 + 4;
}

entry
{
    /* nested functions are forbidden */
    inner_function(a: int): int
    {
        return a + 3;
    }

    /* OK, wait label in function */
    [my_label_here]

    /* type declaration statement in function */
    type mytype: int;
}
