int printf (const char *, ...);
struct
{
    int f3;
    int f4;
    int f6;
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
    printf ("%d\n", a.f6);
    return 0;
}
