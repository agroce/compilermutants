int printf (const char *, ...);
struct S0
{
    int f4;
    int f7;
};
struct S1
{
    struct S0 f1;
    struct S0 f6;
    int f7;
}
a, *b = &a;
int
main ()
{
    struct S1 c = { 0, 0, 0, 1 };
    *b = c;
    *b = *b;
    printf ("%d\n", a.f6.f7);
    return 0;
}
