int printf (const char *, ...);
int a, b;
static int *c = &a, *e = &b;
static const int *d = &a;
int
main ()
{
    a = 1;
    *e = *d;
    *c = 0;
    printf ("%d\n", b);
    return 0;
}
