int printf (const char *, ...);
int a, b, d, e, g;
static int *c = &b;
int fn1 ();
int *
fn2 (int *p1)
{
    int *f = &b;
    d = fn1 (p1);
    for (;;)
    {
        e = 0;
        for (; e <= 0; e = 1)
            if (*c)
                return 0;
        *f = 1;
    }
}

int
fn1 (int *p1)
{
    *p1 = g;
    return 0;
}

int
main ()
{
    int *h = &a, *i = &b;
    *i = g;
    fn2 (h);
    *c = 0;
    printf ("%d\n", a);
    return 0;
}
