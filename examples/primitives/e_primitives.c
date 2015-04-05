/*
File: e_primitives.c

This file is part of the Epiphany BSP library.

Copyright (C) 2014 Buurlage Wits
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

#include <e_bsp.h>
#include "e-lib.h"

// Get initial data from the host
// By concatenating all messages after each other into the buffer
// As input, nbytes contains the maximum buffer size
// On return, nbytes contains the amount of bytes that were retrieved
void get_initial_data(void *buffer, unsigned int *nbytes);

int main()
{
    // Initialize
    bsp_begin();

    int n = bsp_nprocs(); 
    int p = bsp_pid();

    // Get the initial data from the host
    // Note that this must happen before the first call to bsp_sync
    float data_buffer[1000];
    unsigned int nbytes = sizeof(data_buffer);
    get_initial_data(&data_buffer, &nbytes);

    // Register a variable
    float squaresums[16];
    bsp_push_reg(&squaresums, sizeof(squaresums));
    bsp_sync();

    // Do computations on data
    int data_count = nbytes / sizeof(float);
    float sum = 0.0f;
    for (int i = 0; i < data_count; i++)
        sum += data_buffer[i] * data_buffer[i];

    // Send result to processor 0
    bsp_hpput(0, &sum, &squaresums, p*sizeof(float), sizeof(float));
    bsp_sync();

    if (p == 0)
    {
        sum = 0.0f;
        for (int i = 0; i < 16; i++)
            sum += squaresums[i];

        ebsp_message("Total square sum is %f", sum);

    }

    // Allocate external (slow, but larger) memory
    // Use ebsp_ext_malloc 100 times per core to check if it works
    void *ptrs[100];
    void *allptrs[16][100];
    bsp_push_reg(&allptrs, sizeof(allptrs));
    bsp_sync();
    for (int i = 0; i < 100; i++)
    {
        ptrs[i] = ebsp_ext_malloc(1);
        bsp_hpput(0, &ptrs[i], &allptrs, (size_t)&allptrs[p][i]-(size_t)&allptrs[0][0], sizeof(void*));
    }
    bsp_sync();

    if (p==0)
    {
        for (int a = 0; a < n; ++a)
            for (int i = 0; i < 100; i++)
                ebsp_message("core %d allocated %p", a, allptrs[a][i]);
    }

    for (int i = 0; i < 100; i++)
        ebsp_free(ptrs[i]);

    if (p == 0)
    {
        // Send back the result
        int tag = 1;
        ebsp_send_up(&tag, &sum, sizeof(float));
    }

    // Finalize
    bsp_end();

    return 0;
}

void get_initial_data(void *buffer, unsigned int *nbytes)
{
    // Get the initial data from the host
    int packets;
    int accum_bytes;
    int tagsize;
    int bufsize = *nbytes;

    bsp_qsize(&packets, &accum_bytes);
    tagsize = ebsp_get_tagsize();

    // Double-check if the host has set the proper tagsize
    if (tagsize != 4)
    {
        bsp_abort("ERROR: tagsize is %d instead of 4", tagsize);
        return;
    }

    // Output some info, but only from core 0 to prevent spamming the console
    int p = bsp_pid();
    if (p == 0)
    {
        ebsp_message("Queue contains %d bytes in %d packet(s).",
                accum_bytes, packets);

        if (accum_bytes > bufsize)
            ebsp_message("Received more bytes than local buffer could hold.");
    }

    int status;
    int tag;
    int offset = 0;

    for (int i = 0; i < packets; i++)
    {
        // Get message tag and size
        bsp_get_tag(&status, &tag);
        if (status == -1)
        {
            ebsp_message("bsp_get_tag failed");
            break;
        }
        // Truncate everything that does not fit
        // Note that local variables will be optimized into registers
        // so do not worry about using an excessive amount of local variables
        // since there are more than 50 non-reserved registers available
        int space = bufsize - offset;
        if (status > space)
            status = space;

        // Get message payload
        bsp_move((void*)((int)buffer + offset), status);
        offset += status;

        if (p==0)
            ebsp_message("Received %d bytes message with tag %d", status, tag);
    }
    *nbytes = offset;
}
