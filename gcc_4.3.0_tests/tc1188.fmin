int printf (const char *, ...);
int a[7];
static int *b = &a[6];
int c;
void
fn1 (p1)
{
    *b = p1;
}

int
main ()
{
    for (; c <= 1; c++)
    {
        int *d = &a[6];
        if (*b)
            break;
        *b = 1;
        fn1 (*d);
    }
    printf ("%d\n", c);
    return 0;
}
