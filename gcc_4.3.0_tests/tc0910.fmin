int printf (const char *, ...);
int a[] = { 0, 0, 0, 0, 1 };

short b;
static int *c = &a[4];
int
main ()
{
    for (;;)
    {
        b = 0;
        for (; b <= 4; b++)
            *c = a[b];
        break;
    }
    printf ("%d\n", a[4]);
    return 0;
}
