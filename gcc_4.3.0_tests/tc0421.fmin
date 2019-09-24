int printf (const char *, ...);
int a, b;
int *c = &a, *d;
int *
fn1 ()
{
    return &a;
}

int
main ()
{
    d = &a;
    *c = (b = d == fn1 ()) - 7;
    printf ("%d\n", a);
    return 0;
}
