/* -*-Mode: C;-*- */

/**BeginCopyright************************************************************
 *
 * $HeadURL$
 * $Id$
 *
 *---------------------------------------------------------------------------
 * Part of PERFECT Benchmark Suite (hpc.pnnl.gov/projects/PERFECT/)
 *---------------------------------------------------------------------------
 *
 * Copyright ((c)) 2014, Battelle Memorial Institute
 * Copyright ((c)) 2014, Georgia Tech Research Corporation
 * All rights reserved.
 *
 * 1. Battelle Memorial Institute (hereinafter Battelle) and Georgia Tech
 *    Research Corporation (GTRC) hereby grant permission to any person
 *    or entity lawfully obtaining a copy of this software and associated
 *    documentation files (hereinafter "the Software") to redistribute
 *    and use the Software in source and binary forms, with or without
 *    modification.  Such person or entity may use, copy, modify, merge,
 *    publish, distribute, sublicense, and/or sell copies of the
 *    Software, and may permit others to do so, subject to the following
 *    conditions:
 * 
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimers.
 * 
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 * 
 *    * Other than as used herein, neither the name Battelle Memorial
 *      Institute nor Battelle may be used in any form whatsoever without
 *      the express written consent of Battelle.
 * 
 *      Other than as used herein, neither the name Georgia Tech Research
 *      Corporation nor GTRC may not be used in any form whatsoever
 *      without the express written consent of GTRC.
 * 
 *    * Redistributions of the software in any form, and publications
 *      based on work performed using the software should include the
 *      following citation as a reference:
 * 
 *      Kevin Barker, Thomas Benson, Dan Campbell, David Ediger, Roberto
 *      Gioiosa, Adolfy Hoisie, Darren Kerbyson, Joseph Manzano, Andres
 *      Marquez, Leon Song, Nathan R. Tallent, and Antonino Tumeo.
 *      PERFECT (Power Efficiency Revolution For Embedded Computing
 *      Technologies) Benchmark Suite Manual. Pacific Northwest National
 *      Laboratory and Georgia Tech Research Institute, December 2013.
 *      http://hpc.pnnl.gov/projects/PERFECT/
 *
 * 2. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 *    BATTELLE, GTRC, OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 *    STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 *    OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 **EndCopyright*************************************************************/

#include <string.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "wami_utils.h"
#include "wami_gmm.h"

static const float STDEV_THRESH = 2.5f;
static const float INIT_STDEV = 80.0f;
static const float alpha = 0.01f; /* Learning rate */
static const float INIT_WEIGHT = 0.01f;
static const float BACKGROUND_THRESH = 0.9f;
#define ONE_OVER_SQRT_TWO_PI (1.0f / sqrt(2.0f * M_PI))

#define MAGIC_INSTR __asm__ __volatile__("xchg %ecx,%ecx;");


void wami_gmm(
    u8 foreground[WAMI_GMM_IMG_NUM_ROWS][WAMI_GMM_IMG_NUM_COLS],
    float mu[WAMI_GMM_IMG_NUM_ROWS][WAMI_GMM_IMG_NUM_COLS][WAMI_GMM_NUM_MODELS],
    float sigma[WAMI_GMM_IMG_NUM_ROWS][WAMI_GMM_IMG_NUM_COLS][WAMI_GMM_NUM_MODELS],
    float weight[WAMI_GMM_IMG_NUM_ROWS][WAMI_GMM_IMG_NUM_COLS][WAMI_GMM_NUM_MODELS],
    u16 (* const frame)[WAMI_GMM_IMG_NUM_COLS])
{
    const size_t num_pixels = WAMI_GMM_IMG_NUM_ROWS * WAMI_GMM_IMG_NUM_COLS;
    int row, col, k, num_foreground = 0;
    
    memset(foreground, 0, sizeof(u8) * num_pixels);

    MAGIC_INSTR;
    for (row = 0; row < WAMI_GMM_IMG_NUM_ROWS; ++row)
    {
        for (col = 0; col < WAMI_GMM_IMG_NUM_COLS; ++col)
        {
            const u16 pixel = frame[row][col];
            int match = -1;
            float sum = 0.0f, norm = 0.0f;
            int sorted_position = 0;

            for (k = 0; k < WAMI_GMM_NUM_MODELS; ++k)
            {
                /*
                 * C89 does not include fabsf(), so using the double-precision
                 * fabs() function will unnecessarily type-convert to double.
                 */
                if (fabs(pixel - mu[row][col][k])/sigma[row][col][k] < STDEV_THRESH)
                {
                    match = k;
                    break;
                }
            }

            /* Update the weights for all models */
            for (k = 0; k < WAMI_GMM_NUM_MODELS; ++k)
            {
                if (k == match)
                {
                    /* A model matched, so update its corresponding weight. */
                    weight[row][col][match] += alpha *
                        (1.0f - weight[row][col][match]);
                }
                else
                {
                    /* Non-matching models have their weights reduced */
                    weight[row][col][k] *= (1.0f - alpha);
                }
            }

            if (match < 0)
            {
                /*
                 * No distribution matched; replace the least likely distribution.
                 * We keep the entries sorted by significance, so the last entry
                 * is also the least likely.  We do this after updating weights
                 * above so that the initial weight is not immediately down-weighted,
                 * although that means that one update above was wasted. That
                 * update could be avoided.
                 */
                mu[row][col][WAMI_GMM_NUM_MODELS-1] = (float) pixel;
                sigma[row][col][WAMI_GMM_NUM_MODELS-1] = INIT_STDEV;
                weight[row][col][WAMI_GMM_NUM_MODELS-1] = INIT_WEIGHT;
            }

            /* Normalize weights */
            for (k = 0; k < WAMI_GMM_NUM_MODELS; ++k)
            {
                sum += weight[row][col][k];
            }

            assert(sum != 0.0f);

            norm = 1.0f / sum;
            for (k = 0; k < WAMI_GMM_NUM_MODELS; ++k)
            {
                weight[row][col][k] *= norm;
            }

            /* Update mu and sigma for the matched distribution, if any */
            if (match >= 0)
            {
                const float mu_k = mu[row][col][match];
                const float sigma_k = sigma[row][col][match];
                const float sigma_k_inv = 1.0f / sigma_k;
                /*
                 * C89 does not include a single-precision expf() exponential function,
                 * so we instead use the double-precision variant exp(). Same for sqrt()
                 * below.
                 */
                const float rho = alpha * (ONE_OVER_SQRT_TWO_PI * sigma_k_inv) *
                    exp( -1.0f * (pixel-mu_k)*(pixel-mu_k) / (2.0f * sigma_k * sigma_k) );
                mu[row][col][match] = (1.0f - rho) * mu_k + rho * pixel;
                sigma[row][col][match] = sqrt(
                    (1.0f - rho) * sigma_k * sigma_k +
                    rho * (pixel-mu[row][col][match]) * (pixel-mu[row][col][match]));
                assert(sigma[row][col][match] > 0);
            }

            /*
             * weight and sigma for the matched (or new) distribution are the only
             * values that may have changed, so we find the correct location of that
             * new value in the sorted list.  Matches lead to more evidence, and thus
             * higher weight and lower sigma, so we only need to sort "higher".
             */
            sorted_position = 0;
            if (match != 0)
            {
                const int sort_from = (match >= 0) ? match : WAMI_GMM_NUM_MODELS-1;
                const float new_significance = weight[row][col][sort_from] /
                    sigma[row][col][sort_from];
                float other_significance, new_mu, new_sigma, new_weight;
                for (k = sort_from-1; k >= 0; --k)
                {
                    other_significance = weight[row][col][k] /
                        sigma[row][col][k];
                    if (new_significance <= other_significance)
                    {
                        break;
                    }
                }
                if (k == 0)
                {
                    if (other_significance >= new_significance)
                    {
                        sorted_position = 1;
                    }
                    else
                    {
                        sorted_position = 0;
                    }
                }
                else
                {
                    sorted_position = k + 1;
                }

                new_mu = mu[row][col][sort_from];
                new_sigma = sigma[row][col][sort_from];
                new_weight = weight[row][col][sort_from];
                for (k = sort_from; k > sorted_position; --k)
                {
                    mu[row][col][k] = mu[row][col][k-1];
                    sigma[row][col][k] = sigma[row][col][k-1];
                    weight[row][col][k] = weight[row][col][k-1];
                }
                mu[row][col][sorted_position] = new_mu;
                sigma[row][col][sorted_position] = new_sigma;
                weight[row][col][sorted_position] = new_weight;
            }

            /* Now, we need to determine if this pixel is foreground or background. */
            {
                float cumsum = weight[row][col][0];
                int B = 0;
                while (B < WAMI_GMM_NUM_MODELS-1 && cumsum <= BACKGROUND_THRESH)
                {
                    cumsum += weight[row][col][++B];
                }
                foreground[row][col] = (sorted_position > B);
                num_foreground += foreground[row][col];
            }
        }
    }
    MAGIC_INSTR;
}
