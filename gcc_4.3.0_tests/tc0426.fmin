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

, *c = &b;
int
main ()
{
    *c = a;
    *c = *c;
    printf ("%d\n", b.f2);
    return 0;
}
