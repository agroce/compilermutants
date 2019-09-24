int printf (const char *, ...);
unsigned char *const *a;
int b;
int *c = &b;
int
fn1 (int p1, int p2)
{
    return p1 - p2;
}

int
main ()
{
    unsigned char *const **d = &a;
    unsigned char *const **e = &a;
    *c = fn1 (d == e, 6);
    printf ("%d\n", b);
    return 0;
}
