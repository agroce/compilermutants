int printf (const char *, ...);
char a;
short b;
short *d = &b;
char *e;
char *
fn1 ()
{
    char *c = &a;
    return c;
}

int
main ()
{
    int *f = 0;
    e = fn1 ();
    *d = e != &a ^ (0 == f | 10L);
    printf ("%d\n", b);
    return 0;
}
