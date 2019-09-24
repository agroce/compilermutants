int printf (const char *, ...);
int a, b;
int
fn1 (int p1, int p2)
{
    return p1 - p2;
}

int
main ()
{
    int *c = &a, *d = &a;
    b = fn1 (d == c, 7);
    *d = b;
    printf ("%d\n", a);
    return 0;
}
