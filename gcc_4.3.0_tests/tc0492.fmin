int printf (const char *, ...);
struct S0
{
    int f7;
}
a, c, f =
{
    1
}

, *b = &a, **const d = &b;
static struct S0 **const *e = &d;
int
main ()
{
    struct S0 **g = &b;
    *g = &c;
    *d = **e;
    ***e = f;
    printf ("%d\n", c.f7);
    return 0;
}
