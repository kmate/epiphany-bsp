/*
This file is part of the Epiphany BSP library.

Copyright (C) 2014-2015 Buurlage Wits
Support e-mail: <info@buurlagewits.nl>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License (LGPL)
as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
and the GNU Lesser General Public License along with this program,
see the files COPYING and COPYING.LESSER. If not, see
<http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <host_bsp.h>

int main(int argc, char** argv) {
    bsp_init("e_bsp_abort.srec", argc, argv);

    bsp_begin(bsp_nprocs());
    int result = ebsp_spmd();
    // expect: ((BSP) ERROR: bsp_abort was called)
    bsp_end();

    // expect: (result: 0)
    printf("result: %i\n", result);

    bsp_init("e_bsp_empty.srec", argc, argv);

    bsp_begin(bsp_nprocs());
    int result_empty = ebsp_spmd();
    bsp_end();

    // expect: (result: 1)
    printf("result: %i\n", result_empty);

    return 0;
}
