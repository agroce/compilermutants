int printf (const char *, ...);
static int a = 6, b, e;
static int *c = &b, *d;
int
main ()
{
    d = &b;
    e = d != c | a;
    if (e)
        printf ("%d\n", 0);
    return 0;
}
