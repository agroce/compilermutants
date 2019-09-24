int printf (const char *, ...);
int a, b;
int *e = &b;
void
fn1 (int *p1, int *p2)
{
    int *c = &a, *d = &a;
    *p1 = (c != d) - *p2;
}

int
main ()
{
    int f = 1;
    fn1 (e, &f);
    printf ("%d\n", b);
    return 0;
}
