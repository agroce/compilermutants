int printf (const char *, ...);
static int a, b = 1, d;
static const int *c = &b;
int *e, *f = &d;
int
main ()
{
    e = &b;
    a = 0;
    for (; a <= 1; a++)
    {
        *f = *c;
        *e = 0;
    }
    printf ("%d\n", d);
    return 0;
}
