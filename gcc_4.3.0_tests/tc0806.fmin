int printf (const char *, ...);
struct S0
{
    int f1;
}
a[7][1];
static struct S0 *b = &a[6][0];
int c = 1;
int
main ()
{
    int *d = &a[6][0].f1;
    for (; c >= 0; c--)
    {
        a[0][0] = *b;
        *d = 1;
    }
    *d = 0;
    printf ("%d\n", a[0][0].f1);
    return 0;
}
