int printf (const char *, ...);
short a;
short *b, *c = &a;
void
fn1 (p1)
{
    short **d = &b;
    *c = (d == &b) >= p1;
}

int
main ()
{
    fn1 (8);
    printf ("%d\n", a);
    return 0;
}
