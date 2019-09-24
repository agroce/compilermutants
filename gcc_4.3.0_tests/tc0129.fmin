int printf (const char *, ...);
struct
{
    int f0;
    int f1;
}
b, c =
{
    0, 1
};

int a;
int
main ()
{
    b = c;
    a = 1;
    for (; a; --a)
        b = b;
    printf ("%d\n", b.f1);
    return 0;
}
