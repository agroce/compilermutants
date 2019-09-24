int printf (const char *, ...);
struct
{
    int f6;
    int f8;
    int f9;
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
    printf ("%d\n", a.f9);
    return 0;
}
