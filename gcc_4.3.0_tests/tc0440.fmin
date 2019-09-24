int printf (const char *, ...);
int a, c;
int *b = &a;
static int *d = &c;
int
fn1 (int p1, int p2)
{
    return p1 * p2;
}

int
main ()
{
    int *e = &c;
    *d = fn1 (e == d, 4073709551613);
    *b = *e;
    printf ("%d\n", a);
    return 0;
}
