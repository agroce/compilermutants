int printf (const char *, ...);
struct S0
{
    int f1;
    int f2;
    int f3;
}
a, c =
{
    1
};

static struct S0 *b = &a;
int
main ()
{
    struct S0 *d = &a;
    *d = c;
    *b = *b;
    printf ("%d\n", a.f1);
    return 0;
}
