int printf (const char *, ...);
struct
{
    int f2;
    int f3;
    int f5;
}
a, b =
{
    0, 0, 1
}

, *c = &a;
int
main ()
{
    *c = b;
    *c = *c;
    printf ("%d\n", a.f5);
    return 0;
}
