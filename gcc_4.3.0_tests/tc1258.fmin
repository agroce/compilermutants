int printf (const char *, ...);
struct S1
{
    int f1;
    int f5;
    int f6;
    int f7;
}
a;
struct S2
{
    struct S1 f3;
};
int
main ()
{
    struct S2 b = { };
    b = b;
    a = b.f3;
    printf ("%d\n", a.f1);
    return 0;
}
