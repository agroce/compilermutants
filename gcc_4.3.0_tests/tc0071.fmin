int printf (const char *, ...);
char a;
int b, c;
static const int *d = &b;
char *e = &a;
int *f = &c;
int
fn1 (p1)
{
    *f = p1;
    return 0;
}

int
main ()
{
    int *g = &b;
    *g = *e++ || 1;
    *g = fn1 (*d);
    printf ("%d\n", c);
    return 0;
}
