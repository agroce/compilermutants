int printf (const char *, ...);
short a;
short *b = &a;
unsigned char *c;
int d;
void
fn1 (p1)
{
    unsigned char **e = &c;
    d = p1 > (e == &c);
    *b = d;
}

int
main ()
{
    fn1 (5);
    printf ("%d\n", a);
    return 0;
}
