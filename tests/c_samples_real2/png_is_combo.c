/* libpng-1.6.44/contrib/libtests/pngimage.c : is_combo (see LICENSES.md) */
static int
is_combo(int transforms)
{
   return transforms & (transforms-1); /* non-zero if more than one set bit */
}
