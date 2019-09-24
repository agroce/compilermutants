int printf (const char *, ...);
int a;
int *b = &a, *c = &a, *d;
int
main ()
{
    {
        int e = 1;
        if (a)
            d = &e;
    }
    {
        int f = 0;
        *b = 0;
        if (f)
            *c = 1;
        int *g = &f;
        d = g;
    }
    printf ("%d\n", a);
    return 0;
}
