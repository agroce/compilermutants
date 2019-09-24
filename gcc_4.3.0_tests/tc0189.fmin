int printf (const char *, ...);
struct S0
{
    int f0:11;
    int f1:22;
    int f2:1;
    int f3:1;
}
a, b =
{
    0, 0, 0, 1
};

int
main ()
{
    struct S0 c = b;
    a = c;
    int d = c.f1;
    printf ("%d\n", a.f3);
    return 0;
}
