int printf (const char *, ...);
struct
{
    int f2;
    int f4;
    int f5;
}
a, d =
{
    0, 0, 1
}

, *b = &a, *c = &a;
int
main ()
{
    *c = d;
    *c = *b;
    printf ("%d\n", a.f5);
    return 0;
}
