int printf (const char *, ...);
struct
{
    int f6;
    int f7;
    int f8;
}
a, c =
{
    0, 0, 1
}

, *b = &a;
int
main ()
{
    *b = c;
    *b = *b;
    printf ("%d\n", a.f8);
    return 0;
}
