int printf (const char *, ...);
int a, b;
int *c = &b;
int
fn1 (char p1)
{
    return p1;
}

void
fn2 (int *p1)
{
    int *d = &a;
    int e = 1;
    if (fn1 (e + (d == p1)));
    else
        *c = 1;
}

int
main ()
{
    fn2 (&a);
    printf ("%d\n", b);
    return 0;
}
