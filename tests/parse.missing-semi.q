entry: int
{
    const a, b, c: byte = 5;

    if c == 5 {
        (a + 3) != b /* missing ; here */
    }

    return 0:int;
}
