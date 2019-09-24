int printf (const char *, ...);
int a, d;
char b;
char *c = &b;
int
fn1 (int p1, int p2)
{
    return p1 - p2;
}

int
main ()
{
    int *e = &a, *f = &a;
    d = fn1 (e == f, 9);
    *f = d;
    *c = *e;
    printf ("%d\n", b);
    return 0;
}
