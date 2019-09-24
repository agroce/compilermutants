int printf (const char *, ...);
struct S0
{
    int f2;
    int f3;
    int f4;
    int f5;
    int f6;
}
a, *b = &a;
int
main ()
{
    struct S0 c = { 0, 0, 0, 1 };
    *b = c;
    *b = *b;
    printf ("%d\n", a.f5);
    return 0;
}
