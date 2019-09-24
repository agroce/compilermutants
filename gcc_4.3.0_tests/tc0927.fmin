int printf (const char *, ...);
int a[8][1];
static int *b = &a[3][0];
int c;
int
main ()
{
    int *d = &a[3][0], *e = &a[3][0];
    *d = 1;
    *e = *b;
    for (; c < 8; c++)
        printf ("%d\n", a[c][0]);
    return 0;
}
