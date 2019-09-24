int printf (const char *, ...);
struct S0
{
    int f3;
    int f4;
    int f5;
    int f6;
}
a;
struct S1
{
    struct S0 f4;
    int f7;
};
int
main ()
{
    struct S1 b = { };
    b = b;
    a = b.f4;
    printf ("%d\n", a.f5);
    return 0;
}
