int printf (const char *, ...);
int a, b;
int *c = &b, *e = &a;
void
fn1 ()
{
    int *d = &b;
    *d |= 8;
}

int
main ()
{
    int **f = &c;
    b = f != &c;
    fn1 ();
    *e = *c;
    printf ("%d\n", a);
    return 0;
}
