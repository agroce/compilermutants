int printf (const char *, ...);
int a = 1;
int *const b = &a;
static int *c = &a;
static const int *d = &a;
int
main ()
{
    *b = 0;
    *c = *d;
    printf ("%d\n", a);
    return 0;
}
