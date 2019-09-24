int printf (const char *, ...);
struct
{
    int f0;
    long f1;
}
a, d =
{
    0, 1
}

, *b = &a, *c = &a;
int
main ()
{
    *b = d;
    *b = *c;
    printf ("%d\n", (int) a.f1);
    return 0;
}
