int printf (const char *, ...);
int a[] = { 0, 0, 0, 1 };

static int *b = &a[3];
int
main ()
{
    int *c = &a[3];
    *b = 0;
    *b = *c;
    printf ("%x\n", a[3]);
    return 0;
}
