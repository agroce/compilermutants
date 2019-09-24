int printf (const char *, ...);
struct
{
    int f0;
    int f4;
    int f5;
}
a, b =
{
    1
}

, *c = &a;
int
main ()
{
    *c = b;
    *c = *c;
    printf ("%d\n", a.f0);
    return 0;
}
