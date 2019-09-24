int printf (const char *, ...);
struct S0
{
    int f0:12;
    char f1;
    int f2:9;
    int f4:1;
};
struct
{
    struct S0 f3;
}
b =
{
    0, 0, 0, 1
};

int a;
int
main ()
{
    struct S0 c = b.f3;
    a = c.f4;
    printf ("%d\n", a);
    return 0;
}
