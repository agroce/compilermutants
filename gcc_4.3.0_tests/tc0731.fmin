int printf (const char *, ...);
char a[6][8][3];
static char *b = &a[5][7][2];
int c;
static int d;
int
main ()
{
    d = 0;
    for (; d <= 1; d = d + 1)
    {
        c = 0;
        for (; c <= 0; c = 1)
            if (a[5][7][2])
                break;
        *b = 1;
    }
    printf ("%d\n", c);
    return 0;
}
