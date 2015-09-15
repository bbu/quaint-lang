a: int
{
    /* returning void but function requires int */
    return;

    /* returning byte but function requires int */
    return 5;

    /* OK */
    return 5:int;
}

b
{
    /* returning byte but function requires void */
    return 6;

    /* OK */
    return;
}

c: ptr(struct(x: u64, y: u64))
{
    correct: ptr(struct(x: u64, y: u64));
    incorrect: ptr(struct(x: u32, y: u64));

    /* type mismatch */
    return incorrect;

    /* OK */
    return correct;
}
