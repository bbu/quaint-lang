entry
{
    my_autovar: int;

    {
        another_autovar: byte = 0;
    }

    {
        /* OK, the previous declaration is in a different lexical block */
        another_autovar: int;

        {
            /* this shadows the outer variable */
            another_autovar: int;
        }
    }

    my_autovar: byte;
}

my_gvar: int[5];
const another_gvar: uptr;

do_some_work: byte
{
    return 0;
}

/* different signature but same name */
do_some_work(a: byte): int
{
    return a:int + 0:int;
}

/* functions and variables live in the same namespace, so this is also a dupe */
do_some_work: int;

/* attempt to redefine a builtin */
exit: vptr;

/* my_gvar is already defined at unit scope */
my_gvar: long;
