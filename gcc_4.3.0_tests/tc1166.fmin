int printf (const char *, ...);
int a = 1, c;
static const int *b = &a;
int
main ()
{
    int d;
    {
        int *e = &a;
        *e = 0;
        d = *b;
    }
    if (d)
    {
        {
            int *f = &a;
            *f = 0;
        }
    }
    else
    {
        {
            int *g = &a;
            *g = 0;
        }
        c++;
    }
    printf ("%d\n", c);
    return 0;
}
