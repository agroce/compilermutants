int printf (const char *, ...);
int a, c;
char b;
char *d;
int *e = &a;
void
fn1 (p1)
{
    *e = p1;
}

int
main ()
{
    d = &b;
    c = 3;
    fn1 (d == &b ^ c);
    printf ("%d\n", a);
    return 0;
}
