int printf (const char *, ...);
int a, b = 1;
int
fn1 (p1)
{
    return p1 << 1;
}

int
main ()
{
    int *c = &a, *d = &a;
    if (fn1 (c == d))
        b = 0;
    printf ("%d\n", b);
    return 0;
}
