int printf (const char *, ...);
struct S0
{
    int f0;
    int f4;
    int f5;
    int f8;
    int f9;
}
a, *b = &a;
int
main ()
{
    struct S0 c = { 1 };
    *b = c;
    *b = *b;
    printf ("%x\n", a.f0);
    return 0;
}
