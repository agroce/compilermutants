int printf (const char *, ...);
int a;
static int *b = &a;
static const int *c = &a;
int *const d = &a;
int
main ()
{
    *b = 1;
    *d = *c;
    printf ("%d\n", a);
    return 0;
}
