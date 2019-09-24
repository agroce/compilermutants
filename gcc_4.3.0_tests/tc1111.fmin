int printf (const char *, ...);
short a;
short *b = &a;
static short **c = &b;
int d, e;
int
main ()
{
    d = c == &b;
    e = 1 + d;
    *b = e;
    printf ("%d\n", a);
    return 0;
}
