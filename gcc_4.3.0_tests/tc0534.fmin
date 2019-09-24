int printf (const char *, ...);
struct S0
{
    int f0;
    int f1;
    int f2;
    int f3;
    int f4;
}
a, *b = &a;
int
main ()
{
    struct S0 c = { 0, 0, 0, 0, 1 };
    *b = c;
    *b = *b;
    printf ("%d\n", a.f4);
    return 0;
}
