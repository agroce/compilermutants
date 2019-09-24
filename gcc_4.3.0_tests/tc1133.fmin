int printf (const char *, ...);
struct
{
    int f0;
    int f2;
    int f3;
}
a =
{
    1
}

, b[1][2][1];
int
main ()
{
    b[0][1][0] = a;
    b[0][1][0] = b[0][1][0];
    printf ("%d\n", b[0][1][0].f0);
    return 0;
}
