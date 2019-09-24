int printf (const char *, ...);
int a, e;
int *b, *c;
int **d, **f = &c;
int
fn1 (int p1, int p2)
{
    return p1 - p2;
}

int
main ()
{
    e = fn1 ((d = &b) == &b, 11);
    *f = &a;
    *c = e;
    printf ("%d\n", a);
    return 0;
}
