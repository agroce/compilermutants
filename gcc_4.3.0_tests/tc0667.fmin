int printf (const char *, ...);
short a;
char b;
short *c = &a;
void
fn1 (p1)
{
    char *d = &b;
    *c = (&b == d) < p1;
}

int
main ()
{
    fn1 (2);
    printf ("%d\n", a);
    return 0;
}
