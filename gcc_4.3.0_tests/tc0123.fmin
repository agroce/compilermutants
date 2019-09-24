int printf (const char *, ...);
struct S1
{
    int f1;
    long f2;
    int f3;
    int f4;
}
a;
int
main ()
{
    struct S1 b = { 0, 0, 0, 1 };
    a = b;
    struct S1 *c = &a;
    *c = a;
    printf ("%d\n", a.f4);
    return 0;
}
