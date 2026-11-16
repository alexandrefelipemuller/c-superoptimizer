/* libpng-1.6.44/contrib/libtests/pngimage.c : first_transform (see LICENSES.md) */
static int
first_transform(int transforms)
{
   return transforms & -transforms; /* lowest set bit */
}
