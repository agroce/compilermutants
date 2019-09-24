int printf (const char *, ...);
int a = 1;
short b;
static const int *c = &a;
static int *d = &a;
short *e = &b;
int
main ()
{
    a = 0;
    *e = *c;
    *d = 0;
    printf ("%d\n", b);
    return 0;
}
