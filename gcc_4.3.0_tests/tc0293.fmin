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

, *c = &a;
int
main ()
{
    *c = b;
    *c = *c;
    printf ("%x\n", a.f3);
    return 0;
}
