int printf (const char *, ...);
struct
{
    int f0;
    int f3;
    int f7;
}
a =
{
    1
}

, b, c, *d = &a, *e = &a;
int
main ()
{
    *e = b;
    *e = *e;
    c = *d;
    printf ("%d\n", c.f0);
    return 0;
}
