int printf (const char *, ...);
int a[5];
static int *b = &a[4];
short c;
int
main ()
{
    int *d = &a[4];
    c = 20;
    for (; c; c = c - 2)
        if (*d)
            *b = 0;
        else
            *b = 1;
    printf ("%d\n", a[4]);
    return 0;
}
