int printf (const char *, ...);
struct
{
    int f5;
    int f6;
    int f7;
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
    printf ("%d\n", a.f7);
    return 0;
}
