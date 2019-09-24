int printf (const char *, ...);
int a, b;
int *c = &a;
void
fn1 (p1)
{
    int *d = &b;
    *c = (d == &b) >= p1;
}

int
main ()
{
    fn1 (5);
    printf ("%d\n", a);
    return 0;
}
