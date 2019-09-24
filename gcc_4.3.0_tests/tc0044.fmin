int printf (const char *, ...);
short a;
static short *b = &a;
int c;
int
fn1 (int p1, int p2)
{
    return p1 + p2;
}

int
main ()
{
    c = fn1 (b != &a, 11);
    *b = c;
    printf ("%d\n", a);
    return 0;
}
