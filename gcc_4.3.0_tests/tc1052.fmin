int printf (const char *, ...);
struct
{
    int f0;
    int f3;
    int f4;
}
a, b =
{
    1
}

, *c = &a;
int
main ()
{
    *c = b;
    *c = *c;
    printf ("%x\n", a.f0);
    return 0;
}
