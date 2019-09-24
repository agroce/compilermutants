int printf (const char *, ...);
unsigned char a;
int b, d;
void
fn1 (int *p1, int p2)
{
    unsigned char *c = &a;
    *p1 = p2 <= (c == &a);
    if (*p1)
        b = 1;
}

int
main ()
{
    fn1 (&d, 11);
    printf ("%d\n", b);
    return 0;
}
