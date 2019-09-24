int printf (const char *, ...);
int a, b;
unsigned c;
int *d = &b, *e = &b;
void
fn1 (p1)
{
    unsigned *f = &c, *g = &c;
    *e = p1 > !(f != g);
}

int
main ()
{
    fn1 (5);
    a = *d;
    printf ("%d\n", a);
    return 0;
}
