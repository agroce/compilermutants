int printf (const char *, ...);
int a[5];
static int *b = &a[4];
char c;
char *d = &c;
int
main ()
{
    int *e = &a[4];
    *b = 1;
    *d = *e;
    *b = 0;
    printf ("%d\n", c);
    return 0;
}
