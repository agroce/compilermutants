int printf (const char *, ...);
int a;
unsigned *b;
static unsigned **c = &b;
int *d = &a;
int
main ()
{
    unsigned **e = &b;
    *d = c != e;
    *d |= 5;
    printf ("%d\n", a);
    return 0;
}
