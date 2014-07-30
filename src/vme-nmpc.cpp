/*
 * vme-nmpc.cpp
 * Author : Timothy A.V. Teatro
 * Date   : 2013-06-10
 *
 * This file is part of vme-nmpc.
 *
 * Copyright (C) 2013, 2014 - Timothy A.V. Teatro
 *
 * vme-nmpc is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * vme-nmpc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vme-nmpc. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

#include "vme-nmpc.h"

#define MAX_SD_ITER 25000
#define MAX_NMPC_ITER 3000

#define SEC_TO_USEC 1e-6

#define CASE 14

int main( int argc, char **argv )
{

    void
    ( *hook_print_SE )( const qnu*, const Lagr*, const nmpc& ) = NULL;
    void
    ( *hook_print_LG )( const qnu*, const Lagr*, const nmpc&, const float* ) = NULL;
    void
    ( *hook_print_SD )( const unsigned int*, const double* ) = NULL;
    void
    ( *hook_print_TR )( const unsigned int*, const double* ) = NULL;
    double
    ( *hook_exec_control_horiz )( qnu*, const nmpc&, robot* ) = NULL;

    char errnote[256];
    unsigned int sd_loop = 0, k = 0, current_tgt_no = 0;
    float tgtdist, grad_dot_grad = 0.;
    robot vme;

    double sd_loop_time, time_last_cmd_sent = 0, now;

    nmpc C;
    cl_opts clopts =
    {
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        "\0"
    };
    parse_command_line( argc, argv, &vme, &clopts );
    parse_input_file( C, vme.conffile() );
    print_greeting( C );

    qnu* qu = ( qnu* ) calloc( C.N, sizeof( qnu ) );
    Lagr* p = ( Lagr* ) calloc( C.N, sizeof( Lagr ) );
    //cmd* cmd_horiz = (cmd*) calloc(C.C, sizeof(cmd));
    float* grad = ( float* ) calloc( C.N + 1, sizeof( float ) );
    float* last_grad = ( float* ) calloc( C.N + 1, sizeof( float ) );
    double* time_to_tgt = ( double* ) calloc( C.ntgt, sizeof( float ) );

    point* endp = ( point* ) malloc( sizeof( point ) );

    C.cur_tgt = C.tgt;
    C.control_step = 0;

    float trip_cost = 0.;

    init_qu_and_p( qu, p, C );

    globalPath gPath;
    gPath.gtgt.x = C.tgt[C.ntgt - 1];
    gPath.gtgt.y = C.tgt[C.ntgt];
#if CASE <= 6
    gPath.refreshPath1r( qu, p, C );
#else
    gPath.refreshPath2r( qu, p, C );
#endif
    for ( int o = 0; o < gPath.Path.size(); ++o )
    {
        printf( "#!!! %f %f\n", gPath.Path[o].x, gPath.Path[o].y );
    }
    gPath.samplePath( C );

    tgtdist = C.tgttol + 1; // Just to get us into the waypoint loop.
    C.horizon_loop = 0;

    /*
     * This next block of decisions sort out the hooks used to print output.
     * Rather than include the ifs in each loop, I'm using function pointers
     * which are set at runtime based on CL flags for verbosity.
     */
    if ( clopts.selec_verbose )
    {
        hook_print_SE = &print_pathnerr;
        hook_print_LG = &print_LG;
        hook_print_SD = &print_SD;
        hook_print_TR = &print_TR;
    }
    else if ( clopts.selec_verbose )
    {
        hook_print_SE = &empty_output_hook;
        hook_print_LG = &empty_output_hook;
        hook_print_SD = &empty_output_hook;
        hook_print_TR = &empty_output_hook;
    }
    else
    {
        if ( clopts.selec_state_and_error_SE )
            hook_print_SE = &print_pathnerr;
        else
            hook_print_SE = &empty_output_hook;

        if ( clopts.selec_lagrange_grad_LG )
            hook_print_LG = &print_LG;
        else
            hook_print_LG = &empty_output_hook;

        if ( clopts.selec_SD_converged_SD )
            hook_print_SD = &print_SD;
        else
            hook_print_SD = &empty_output_hook;

        if ( clopts.selec_target_reached_TR )
            hook_print_TR = &print_TR;
        else
            hook_print_TR = &empty_output_hook;
    }
    if ( clopts.selec_sim )
        hook_exec_control_horiz = &exec_control_horiz_dummy;
    else
        hook_exec_control_horiz = &exec_control_horiz_vme;

    if ( !clopts.selec_sim )
    {
        vme.tcp_connect();
        vme.update_poshead( qu, C );
    }

    /*
     * Enter the loop which will take us through all waypoints.
     */
    while ( current_tgt_no < C.ntgt )
    {
        time_to_tgt[current_tgt_no] = -wall_time();
        C.cur_tgt = &C.tgt[current_tgt_no * 2];
        tgtdist = C.tgttol + 1;
        while ( tgtdist > .1 )
        {
            C.horizon_loop += 1;
            sd_loop = 0;
            sd_loop_time = -wall_time();
            /*
             * SD loop.
             */
            while ( 1 )
            {
                sd_loop += 1;
                grad_dot_grad = 0.;
                /*
                 * The core of the gradient decent is in the next few lines:
                 */
                tgtdist = predict_horizon( qu, p, C );

#if CASE == 1
                set_tracking_errors( qu, p, C );
                get_gradient( qu, p, C, grad );
#elif CASE == 2
                set_tracking_errors( qu, p, C );
                get_gradient( qu, p, C, grad );
#elif CASE == 3
                gPath.setExEy( qu, p, C );
                get_gradient( qu, p, C, grad );
#elif CASE == 4
                gPath.refreshPath1r( qu, p, C );
                gPath.samplePath( C );
                gPath.setExEy( qu, p, C );
                get_gradient( qu, p, C, grad );
#elif CASE == 5
                set_tracking_errors( qu, p, C );
                gPath.refreshPath1r( qu, p, C );
                gPath.GPE( qu, C, endp );
                get_gradient_setEP( qu, p, C, grad, endp );
#elif CASE == 6
                gPath.refreshPath1r( qu, p, C );
                gPath.samplePath( C );
                gPath.setExEy( qu, p, C );
                gPath.GPE( qu, C, endp );
                get_gradient_setEP( qu, p, C, grad, endp );
#elif CASE == 7 // A + TR
                set_tracking_errors( qu, p, C );
                get_gradient( qu, p, C, grad );
#elif CASE == 8 // WP + TR
                gPath.refreshPath2r( qu, p, C );
                set_tracking_errors( qu, p, C );
                C.cur_tgt[0] = gPath.Path[1].x;
                C.cur_tgt[1] = gPath.Path[1].y;
                get_gradient( qu, p, C, grad );
#elif CASE == 9 // SGP + TR
                gPath.setExEy( qu, p, C );
                get_gradient( qu, p, C, grad );
#elif CASE == 10 // DGP + TR
                gPath.refreshPath2r( qu, p, C );
                gPath.samplePath( C );
                gPath.setExEy( qu, p, C );
                get_gradient( qu, p, C, grad );
#elif CASE == 11 // A + GPE (tuned)
                set_tracking_errors( qu, p, C );
                gPath.refreshPath2rEP( qu, p, C );
                gPath.GPE( qu, C, endp );
                get_gradient_setEP( qu, p, C, grad, endp );
#elif CASE == 12 // A + GPE + PWS
                set_tracking_errors( qu, p, C );
                gPath.refreshPath2rEP( qu, p, C );
                gPath.GPE( qu, C, endp );
                int G = gPath.GetG( C );
                get_gradient_scaleG_setEP( qu, p, C, grad, G, endp );
#elif CASE == 13 // DGP + AE
                gPath.refreshPath2r( qu, p, C );
                gPath.samplePath( C );
                gPath.setExEy( qu, p, C );
                gPath.AE( qu, C, endp );
                get_gradient_setEP( qu, p, C, grad, endp );
#elif CASE == 14 // DGP + GPE + PWS
                gPath.refreshPath2r( qu, p, C );
                gPath.samplePath( C );
                gPath.setExEy( qu, p, C );
                gPath.refreshPath2rEP( qu, p, C );
                gPath.GPE( qu, C, endp );
                int G = gPath.GetG( C );
                get_gradient_scaleG_setEP( qu, p, C, grad, G, endp );
#endif
                for ( k = 0; k < C.N; k++ )
                {
                    grad_dot_grad += grad[k] * last_grad[k];
                }
                /*
                 * Detect direction changes in the gradient by inspecting the
                 * product <grad, last_grad>. If it is positive, then the
                 * iterations are successfully stepping to the minimum, and we
                 * can accelerate by increasing dg. If we overshoot (and the
                 * product becomes negative), then backstep and drop dg to a
                 * safe value.
                 */
                if ( grad_dot_grad > 0 )
                {
                    C.dg *= 2;
                    for ( k = 0; k < C.N; ++k )
                    {
                        qu[k].Dth -= C.dg * grad[k];
                    }
                }
                else
                {
                    C.dg = 0.1; // TODO: Adaptive.
                    for ( k = 0; k < C.N; ++k )
                    {
                        qu[k].Dth += C.dg * grad[k];
                    }
                }
                swap_fptr( &grad, &last_grad );
                if ( last_grad[C.N] < .1 )
                    break;
                if ( sd_loop >= MAX_SD_ITER )
                {
                    sprintf( errnote, "Reached %d SD iterations. Stopping.",
                             sd_loop );
                    report_error( EXCEEDED_MAX_SD_ITER, errnote );
                }
            }
            // This changes nothing in the calculation, but makes the endpoint
            // penalty rather visible in the plot_output.py animation.
#if (CASE == 5) || (CASE ==6 ) || (CASE == 11) || (CASE >= 12 )
            p[C.N - 2].ex = endp->x - qu[C.N].x;
            p[C.N - 2].ey = endp->y - qu[C.N].y;
#endif
            hook_print_SE( qu, p, C );
            hook_print_LG( qu, p, C, grad );
            sd_loop_time += wall_time();
            hook_print_SD( &sd_loop, &sd_loop_time );
            C.control_step += C.C;

            hook_exec_control_horiz( qu, C, &vme );

            trip_cost += stage_cost_execp( qu, p, C );

            for ( k = 0; k < C.N - C.C - 1; ++k )
            {
                // cmd_horiz[k].v = qu[k].v;
                // cmd_horiz[k].Dth = qu[k].Dth;
                qu[k].v = qu[k + C.C].v;
                qu[k].Dth = qu[k + C.C].Dth;
            }

            if ( C.control_step > MAX_NMPC_ITER * C.C )
            {
                sprintf( errnote,
                         "Reached %d NMPC steps without reaching tgt. Stopping.",
                         MAX_NMPC_ITER );
                report_error( TRAPPED_IN_NMPC_LOOP, errnote );
            }
            /*
             * The last thing we do is get the new position and heading for
             * the next SD calculation.
             */
            // vme.update_poshead( qu );
        }
        time_to_tgt[current_tgt_no] += wall_time();
        hook_print_TR( &current_tgt_no, &time_to_tgt[current_tgt_no] );
        ++current_tgt_no;
        printf( "# Accumulated cost of executed path: %f\n", trip_cost );
    }

    return 0;
}