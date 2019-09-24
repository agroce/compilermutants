int printf (const char *, ...);
int a, e;
short b;
unsigned *c;
unsigned **d = &c;
short *f = &b;
int
main ()
{
    e = (*d = &a) == &a;
    *f = *c = 6 ^ e;
    printf ("%d\n", b);
    return 0;
}
