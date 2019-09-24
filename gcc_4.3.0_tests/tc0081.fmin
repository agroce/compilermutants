int printf (const char *, ...);
struct S0
{
    int f2;
    int f6;
    int f7;
};
struct S1
{
    int f2;
    int f4;
    struct S0 f5;
}
b, d, *c = &b;
int a;
int
main ()
{
    for (; a <= 1; ++a)
    {
        struct S1 e = { 1 };
        for (;;)
        {
            d = *c;
            break;
        }
        *c = e;
        *c = *c;
    }
    printf ("%d\n", d.f2);
    return 0;
}
