int printf (const char *, ...);
struct
{
    int f1;
    int f4;
}
a, b, c;
int d;
int
main ()
{
    d = b.f1;
    --a.f4;
    b = a;
    c = b = b;
    printf ("%d\n", c.f4);
    return 0;
}
