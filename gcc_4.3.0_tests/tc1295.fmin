int printf (const char *, ...);
int a[] = { 0, 0, 0, 0, 0, 1 };

static int *b = &a[5];
int
main ()
{
    int *c = &a[5];
    *b = 0;
    *b = *c;
    printf ("%d\n", a[5]);
    return 0;
}
