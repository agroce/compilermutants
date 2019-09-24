int printf (const char *, ...);
struct S0
{
    int f0:1;
    char f1;
    int f2:20;
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
    printf ("%d\n", a.f3);
    return 0;
}
