int printf (const char *, ...);
int a, b, f;
void
fn1 (int *p1)
{
    b = *p1;
}

void
fn2 (int *p1)
{
    int c = 3;
    int *d = &a, *e = &a;
    *p1 = c ^ d == e;
}

int
main ()
{
    fn2 (&f);
    fn1 (&f);
    printf ("%d\n", b);
    return 0;
}
