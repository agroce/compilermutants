int printf (const char *, ...);
int *a;
int b;
int *c = &b;
int
fn1 (p1)
{
    return p1 + 1;
}

int
main ()
{
    int **d = &a, **e = &a;
    *c = fn1 (e == d);
    printf ("%d\n", b);
    return 0;
}
