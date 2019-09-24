int printf (const char *, ...);
struct S2
{
    int f0:9;
    char f1;
    short f2;
    char f3;
}
a, c =
{
    0, 0, 0, 1
}

, *b = &a;
int
main ()
{
    struct S2 d;
    {
        d = c;
    } *b = d;
    printf ("%d\n", a.f3);
    return 0;
}
