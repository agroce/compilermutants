int printf (const char *, ...);
int a = 1;
const unsigned *b;
int
fn1 (p1)
{
    return 1 + p1;
}

int
main ()
{
    const unsigned **c = &b, **d = &b;
    if (fn1 (c == d))
        a = 0;
    printf ("%d\n", a);
    return 0;
}
