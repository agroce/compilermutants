int printf (const char *, ...);
struct S1
{
    int f0;
    int f1;
    int f2;
}
a, b =
{
    0, 0, 1
};

int
main ()
{
    struct S1 *c = &a;
    *c = b;
    *c = *c;
    printf ("%d\n", a.f2);
    return 0;
}
