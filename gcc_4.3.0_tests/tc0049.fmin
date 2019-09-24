int printf (const char *, ...);
int a = 1, b, c;
int *e;
int *
fn1 ()
{
    if (a)
    {
        int *d[4];
        for (; b < 4; b++)
            d[b] = &a;
        return d[1];
    }
    return &c;
}

int
main ()
{
    e = fn1 ();
    *e = 0;
    printf ("%d\n", a);
    return 0;
}
