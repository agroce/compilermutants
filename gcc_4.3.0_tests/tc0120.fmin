int printf (const char *, ...);
int a;
int *b = &a;
short *c;
static short **d = &c;
int
fn1 (int p1, int p2)
{
    return p1 * p2;
}

int
main ()
{
    short **e = &c;
    *b = fn1 (d == e, 2);
    printf ("%d\n", a);
    return 0;
}
