int printf (const char *, ...);
union
{
    int f0;
    int f1;
}
c;
int a = 1;
static const int *b = &a;
static int *d = &a;
int
main ()
{
    a = 0;
    for (; c.f0 != 4; c.f0 = 4)
        if (*b)
            break;
    *d = 0;
    printf ("%d\n", c.f1);
    return 0;
}
