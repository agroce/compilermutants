int printf (const char *, ...);
char a;
int b, f;
unsigned *c, *d;
char *e = &a;
int
fn1 (p1)
{
    return 1 + p1;
}

int
main ()
{
    f = fn1 ((c = &b) == (d = &b));
    *e = f;
    printf ("%d\n", a);
    return 0;
}
