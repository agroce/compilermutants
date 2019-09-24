int printf (const char *, ...);
int a, c, e, f = 1;
int *b, *d = &c, *g = &a;
int *
fn1 ()
{
    for (;; e++)
    {
        if (f)
        {
            *g = 1;
            return g;
        }
        if (*d)
            continue;
        return d;
    }
}

int
main ()
{
    int i;
    int **h = &b, **j = &b;
    *h = fn1 ();
    *d = 0;
    *j = 0;
    i = 0;
    for (; i < 6; i++)
        printf ("%x\n", a);
    return 0;
}
