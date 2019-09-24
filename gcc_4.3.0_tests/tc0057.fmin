int printf (const char *, ...);
int a;
unsigned char *b;
int *c = &a;
static unsigned char **d = &b;
int
fn1 (int p1, int p2)
{
    return p1 * p2;
}

int
main ()
{
    unsigned char **e = &b;
    *c = fn1 (2, e == d);
    printf ("%d\n", a);
    return 0;
}
