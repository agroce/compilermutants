int printf (const char *, ...);
int a;
short b;
unsigned short *c;
int *d = &a;
unsigned short *const e = &b;
int
fn1 (int p1, char p2)
{
    return p1 / p2;
}

int
main ()
{
    *d = fn1 (e == (c = &b), 255);
    printf ("%d\n", a);
    return 0;
}
