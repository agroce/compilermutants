int printf (const char *, ...);
struct
{
    int f1;
    int f2;
    int f3;
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
    printf ("%d\n", b.f3);
    return 0;
}
