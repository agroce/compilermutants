int printf (const char *, ...);
int a, b, c = 1, e, f;
static const int *d = &c;
void
fn1 ()
{
    f = a && b && 0 > b || a && b < 0 && b;
}

int
main ()
{
    c = 0;
    for (; c != 1; ++c)
    {
        if (*d)
            continue;
        fn1 ();
        e = 1;
    }
    printf ("%d\n", e);
    return 0;
}
