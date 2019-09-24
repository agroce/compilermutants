int printf (const char *, ...);
int a, b;
int
fn1 (p1)
{
    return 0 == 0 ? p1 : 0;
}

int
main ()
{
    int *c = &a, *d = &a;
    b = fn1 (d == c);
    *d = b ^ 7L;
    printf ("%d\n", a);
    return 0;
}
