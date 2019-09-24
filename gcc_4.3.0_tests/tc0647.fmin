int printf (const char *, ...);
int a, c, d;
int *b = &a, *f;
int **e;
int
fn1 (p1)
{
    return 1 + p1;
}

int
main ()
{
    d = 1;
    for (; d <= 2; d = d + 1)
    {
        c = fn1 ((e = &f) == &f) == ((*b ^= 1) == 0);
        if (c)
            break;
    }
    printf ("%d\n", a);
    return 0;
}
