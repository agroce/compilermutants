int printf (const char *, ...);
int a = 1, c;
static const int *b = &a;
int
main ()
{
    int *d = &a, *e = &a;
    *e = 0;
    c = 0;
    for (; c != 1; c = 1)
        *d = *b;
    printf ("%d\n", a);
    return 0;
}
