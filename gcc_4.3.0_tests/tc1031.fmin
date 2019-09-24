int printf (const char *, ...);
int a = 1, d;
unsigned *b = &a;
static unsigned **c = &b;
int
fn1 (p1)
{
    return p1 + 1;
}

int
main ()
{
    d = fn1 (c == &b);
    d || (**c = 0);
    printf ("%d\n", a);
    return 0;
}
