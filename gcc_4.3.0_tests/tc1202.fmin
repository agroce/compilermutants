int printf (const char *, ...);
struct
{
    int f3;
    int f4;
}
a, b;
int c;
int
main ()
{
    for (; c < 10; c++)
    {
        b = a;
        a.f3 = 0;
        for (; a.f3 >= 0; a.f3--)
            b = b;
    }
    printf ("%d\n", b.f3);
    return 0;
}
