int printf (const char *, ...);
struct
{
    int f5;
}
b, d =
{
    1
};

int a, c;
static int
fn1 ()
{
    for (; a <= 0; a++)
    {
        int *e = &d.f5, *f = &b.f5;
        for (; c; c--)
        {
            if (*e)
                break;
            f = e;
        }
        if (*f)
            return 0;
        *f = 0;
    }
    return 0;
}

int
main ()
{
    fn1 ();
    printf ("%d\n", d.f5);
    return 0;
}
