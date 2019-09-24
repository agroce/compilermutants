int printf (const char *, ...);
struct
{
    int f0;
    int f1;
    int f2;
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
    printf ("%d\n", a.f2);
    return 0;
}
