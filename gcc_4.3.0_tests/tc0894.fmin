int printf (const char *, ...);
short a = 2;
char b;
char *c = &b;
void
fn1 (int p1, short *p2)
{
    short *d = &a;
    *c = (p2 == d) < p1;
}

int
main ()
{
    fn1 (a, &a);
    printf ("%d\n", b);
    return 0;
}
