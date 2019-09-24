int printf (const char *, ...);
struct
{
    int f3:8;
    int f4:25;
    int f5:14;
}
b, c =
{
    0, 0, 1
}

, *e = &b;
int a, d;
int
main ()
{
    *e = c;
    for (;; a++)
    {
        d = 0;
        for (; d <= 29; d = d + 7)
            *e = *e;
        break;
    }
    printf ("%d\n", b.f5);
    return 0;
}
