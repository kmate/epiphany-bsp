/*
File: e_spmd.h

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

#include <math.h>
#include "common.h"

// SPARSE MATRIX-VECTOR MULTIPLICATION
// -----------------------------------
// Algorithm:
// ==========
//
// The algorithm consists of 4 steps. 
// (1) Obtain non-local vector components v_j
//      (a) number of non-local js
//      (b) indices of non-local js
//      (c) owners of non-local js
//      (d) values of non-local js
// (2) Compute contributions (u_i)_s
// (3) Send contributions (u_i)_s to owner of u_i
// (4) Accumulate contributions and obtain vector u = Av
//
// Memory:
// =======
//
// Note that since we assume a completely arbitrary distribution, and we 
// will allocate the memory statically we have the following worst-
// cases:
// - entries: O(nz) * 3
// -       v: O(m) * 2
// -       u: O(n) * 2
// -   v_own: O(m / p)
// -   u_own: O(n / p)
// - (u_i)_s: O(np)
//
// Especially the last one is troubling, but can be solved by using
// a message passing system, dynamic allocation or limiting the load-
// imbalance epsilon. For now we just make the arrays as large as they
// need to be for the worst case. Later we will provide a real spmv 
// that is optimized and can be used in real applications.

int main()
{
    bsp_begin();

    int tagsize = sizeof(int);
    bsp_set_tagsize(&tagsize);

    int nprocs = bsp_nprocs(); 
    int s = bsp_pid();

    // information about distribution
    // note: these are defined for the local (sub)objects
    int nz = 0;
    int nv = 0;
    int nu = 0;

    // information on (local) matrix size
    int nrows = 0;
    int ncols = 0;

    int rows = 0;
    int cols = 0;

    int nmsgs_down = -1;
    int nbytes_down = -1;

    // store matrix in ROW MAJOR order
    float* mat = 0;
    int* mat_inc = 0;

    int* row_index = 0;
    int* col_index = 0;
    int* v_index = 0;
    int* u_index = 0;
    float* v_values = 0;


    bsp_qsize(&nmsgs_down, &nbytes_down);
    for (int msg = 0; msg < nmsgs_down; ++msg)
    {
        // NOTE: here we assume that SPMV_DOWN_TAG is same size on both
        // architectures, with enum classes in C++11 we can guarantee this,
        // but for now we force one tag to be 0xffffffff to force 32 bit int
        SPMV_DOWN_TAG tag = -1;
        int status = -1;
        bsp_get_tag(&status, &tag);

        switch(tag)
        {
            case TAG_ROWS:
                bsp_move(&rows, sizeof(int));
                break;

            case TAG_COLS:
                bsp_move(&cols, sizeof(int));
                break;

            case TAG_MAT:
                if (nz == 0)
                    nz = status / sizeof(int);
                mat = ebsp_ext_malloc(nz * sizeof(float));
                bsp_move(mat, status);
                break;

            case TAG_MAT_INC:
                if (nz == 0)
                    nz = status / sizeof(int);
                mat_inc = ebsp_ext_malloc(nz * sizeof(float));
                bsp_move(mat_inc, status);
                break;

            case TAG_ROW_IDX:
                nrows = status / sizeof(int);
                row_index = ebsp_ext_malloc(nrows * sizeof(int));
                bsp_move(row_index, status);
                break;

            case TAG_COL_IDX:
                ncols = status / sizeof(int);
                col_index = ebsp_ext_malloc(ncols * sizeof(int));
                bsp_move(col_index, status);
                break;

            case TAG_V_IDX:
                nv = status / sizeof(int);
                v_index = ebsp_ext_malloc(nv * sizeof(int));
                bsp_move(v_index, status);
                break;

            case TAG_U_IDX:
                nu = status / sizeof(int);
                u_index = ebsp_ext_malloc(nu * sizeof(int));
                bsp_move(u_index, status);
                break;

            case TAG_V_VALUES:
                if (nv == 0)
                    nv = status / sizeof(int);
                v_values = ebsp_ext_malloc(nu * sizeof(float));
                bsp_move(v_values, status);
                break;

            default:
                bsp_abort("SpMV: Tag not recognized when transferring matrix");
                break;
        }
    }

    // (b) obtain owners and remote indices
    int* v_remote_idxs = ebsp_ext_malloc(ncols * sizeof(int));
    int* v_src_procs = ebsp_ext_malloc(ncols * sizeof(int));
    int* u_remote_idxs = ebsp_ext_malloc(nrows * sizeof(int));
    int* u_src_procs = ebsp_ext_malloc(nrows * sizeof(int));

    //-------------------------------------------------------------------------
    // INITIALIZE (TODO: move to own function for repeated SpMV)
    //-------------------------------------------------------------------------
    //    initialize_src_idxs(s, nprocs, cols, rows, ncols, nrows, nv, nu,
    //            row_index, col_index, v_index, u_index,
    //            v_remote_idxs, v_src_procs, u_remote_idxs, u_src_procs);
 
    // (0) initialize sources and remote indices

    // (a) Store ownership / local idx cyclically
    // allocate
    int* v_src_tmp = ebsp_ext_malloc((cols / nprocs) * sizeof(int));
    int* v_remote_idxs_tmp = ebsp_ext_malloc((cols / nprocs) * sizeof(int));
    int* u_src_tmp = ebsp_ext_malloc((rows / nprocs) * sizeof(int));
    int* u_remote_idxs_tmp = ebsp_ext_malloc((rows / nprocs) * sizeof(int));

    // register vars
    bsp_push_reg((void*)v_src_tmp, (cols / nprocs) * sizeof(int));
    bsp_sync();

    bsp_push_reg((void*)v_remote_idxs_tmp, (cols / nprocs) * sizeof(int));
    bsp_sync();

    bsp_push_reg((void*)u_src_tmp, (rows / nprocs) * sizeof(int));
    bsp_sync();

    bsp_push_reg((void*)u_remote_idxs_tmp, (rows / nprocs) * sizeof(int));
    bsp_sync();

//    ebsp_message("after push regs (%i x %i) (%i x %i) 0x%x 0x%x 0x%x 0x%x %i %i %i",
//        rows,
//        cols,
//        nrows,
//        ncols,
//        (unsigned int)v_src_tmp,
//        (unsigned int)u_src_tmp,
//        (unsigned int)v_index,
//        (unsigned int)u_index,
//        nprocs,
//        nv,
//        nu);


    // distribute own information
    for (int i = 0; i < nv; ++i)
    {
        bsp_put(v_index[i] % nprocs,
                &s,
                v_src_tmp,
                (v_index[i] / nprocs) * sizeof(int),
                sizeof(int));

        bsp_put(v_index[i] % nprocs,
                &i,
                v_remote_idxs_tmp,
                (v_index[i] / nprocs) * sizeof(int),
                sizeof(int));
    }

    // distribute own information
    for (int i = 0; i < nu; ++i)
    {
        bsp_put(u_index[i] % nprocs,
                &s,
                u_src_tmp,
                (v_index[i] / nprocs) * sizeof(int),
                sizeof(int));

        bsp_put(u_index[i] % nprocs,
                &i,
                u_remote_idxs_tmp,
                (u_index[i] / nprocs) * sizeof(int),
                sizeof(int));
    }

    bsp_sync();

    for (int i = 0; i < ncols; ++i)
    {
        // remote local index
        bsp_get(col_index[i] % nprocs,
                v_src_tmp,
                (col_index[i] / nprocs) * sizeof(int),
                &v_src_procs[i],
                sizeof(int));

        bsp_get(col_index[i] % nprocs,
                v_remote_idxs_tmp,
                (col_index[i] / nprocs) * sizeof(int),
                &v_remote_idxs[i],
                sizeof(int));
    }

    for (int i = 0; i < nrows; ++i)
    {
        // remote local index
        bsp_get(row_index[i] % nprocs,
                u_src_tmp,
                (row_index[i] / nprocs) * sizeof(int),
                &u_src_procs[i],
                sizeof(int));

        bsp_get(row_index[i] % nprocs,
                u_remote_idxs_tmp,
                (row_index[i] / nprocs) * sizeof(int),
                &u_remote_idxs[i],
                sizeof(int));
    }

    ebsp_free(v_src_tmp);
    ebsp_free(v_remote_idxs_tmp);
    ebsp_free(u_src_tmp);
    ebsp_free(u_remote_idxs_tmp);

    // pop vars
    bsp_pop_reg((void*)v_src_tmp);
    bsp_sync();

    bsp_pop_reg((void*)v_remote_idxs_tmp);
    bsp_sync();

    bsp_pop_reg((void*)u_src_tmp);
    bsp_sync();

    bsp_pop_reg((void*)u_remote_idxs_tmp);
    bsp_sync();

    //-------------------------------------------------------------------------
    // ACTUAL SPMV
    //-------------------------------------------------------------------------

    float* v_vec = ebsp_ext_malloc(nv * sizeof(float));
    float* u_vec = ebsp_ext_malloc(nu * sizeof(float));

    bsp_push_reg((void*)v_values, nv * sizeof(int));
    bsp_sync();

    for (int i = 0; i < ncols; ++i)
    {
        bsp_get(v_src_procs[i],
                v_values,
                v_remote_idxs[i] * sizeof(int),
                &v_vec[i],
                sizeof(int));
    }

    bsp_sync();

    //-------------------------------------------------------------------------
    // (2) COMPUTE (u_i)_s 
    // (3) SEND (u_i)_s to remote

    int cur_col = 0; //mat_inc[0]; 
    int k = 0;
    float u_i_s = 0.0;
    for (int i = 0; i < nz; ++i)
    {
        if (cur_col > ncols)
        {
            cur_col -= ncols;

            ebsp_message("%f gets sent -> %i", u_i_s, u_src_procs[k]);
            bsp_send(u_src_procs[k], &u_remote_idxs[k], &u_i_s, sizeof(float));
            k += 1;
            u_i_s = 0.0;
        }

        u_i_s += mat[i] * v_vec[cur_col];
        cur_col += mat_inc[i];
    }

    bsp_sync();

    //-------------------------------------------------------------------------
    // (4) COMPUTE u_i

    int nmsgs = -1;
    int nbytes = -1;
    int idx = -1;
    int status = -1;
    float incoming_sum = -1;

    bsp_qsize(&nmsgs, &nbytes);
    ebsp_message("nmsgs: %i", nmsgs);

    for (int k = 0; k < nmsgs; ++k)
    {
        bsp_get_tag(&status, &idx);
        bsp_move(&incoming_sum, sizeof(float));
        ebsp_message("u[%i] += %f\n", idx, incoming_sum);
        u_vec[idx] += incoming_sum;
    }

    bsp_pop_reg((void*)v_values);


    tagsize = sizeof(int);
    bsp_set_tagsize(&tagsize);

    for (int j = 0; j < nu; ++j)
    {
        int tag = u_index[j];
        ebsp_send_up(&tag, &u_vec[j], sizeof(float));
    }

    // FIXME: free everythhing

    bsp_end();

    return 0;
}
