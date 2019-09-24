int printf (const char *, ...);
int a;
char b;
char *c = &b;
int
fn1 (int p1, int p2)
{
    return p1 + p2;
}

int
main ()
{
    int *d = &a, *e = &a;
    *c = fn1 (d != e, 3);
    printf ("%d\n", b);
    return 0;
}
