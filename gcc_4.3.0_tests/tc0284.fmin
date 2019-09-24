int printf (const char *, ...);
int a, b;
int *c = &b;
void
fn1 (p1)
{
    int *d = &a;
    *c = p1 <= (&a == d);
}

int
main ()
{
    fn1 (2);
    printf ("%d\n", b);
    return 0;
}
