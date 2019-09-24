int printf (const char *, ...);
struct S0
{
    int f0:10;
    char f1;
    int f3:9;
    int f4:1;
}
c =
{
    0, 0, 0, 1
};

int a;
int *b = &a;
int
main ()
{
    struct S0 d;
    d = c;
    struct S0 e = d;
    *b = e.f4;
    printf ("%d\n", a);
    return 0;
}
