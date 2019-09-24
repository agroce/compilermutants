int printf (const char *, ...);
struct
{
    int f3;
}
a, b =
{
    1
}

, c, *d = &c, *e = &b;
int
main ()
{
    *e = a;
    *e = b;
    for (;;)
    {
        *d = b;
        break;
    }
    printf ("%d\n", c.f3);
    return 0;
}
